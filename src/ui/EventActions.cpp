#include "EventActions.h"

#include "callie/Places.h"
#include "callie/Repeat.h"
#include "callie/Times.h"

#include <QUrlQuery>

using namespace Qt::StringLiterals;

namespace callie {

Event EventActions::toEvent(const QVariantMap &event)
{
    Event e;
    e.uid = event.value(u"uid"_s).toString();
    e.calendarId = event.value(u"calendarId"_s).toString();
    e.eventId = event.value(u"eventId"_s).toString();
    e.seriesId = event.value(u"seriesId"_s).toString();
    e.summary = event.value(u"summary"_s).toString();
    e.allDay = event.value(u"allDay"_s).toBool();
    e.start = event.value(u"start"_s).toDateTime();
    e.end = event.value(u"end"_s).toDateTime();
    e.recurrenceId = event.value(u"recurrenceId"_s).toDateTime();
    e.zone = event.value(u"zone"_s).toString();
    return e;
}

void EventActions::respond(const QVariantMap &event, const QString &status, bool wholeSeries)
{
    if (!m_source || m_busy)
        return;
    start();
    const QPointer<EventActions> self(this);
    const QString eventId = event.value(u"eventId"_s).toString();
    m_source->respond(toEvent(event), status, wholeSeries,
                      [this, self, eventId, status](const QString &error) {
                          if (!self)
                              return;
                          finish(eventId, error);
                          if (error.isEmpty())
                              Q_EMIT responded(eventId, status);
                      });
}

void EventActions::remove(const QVariantMap &event, bool wholeSeries)
{
    if (!m_source || m_busy)
        return;
    start();
    const QPointer<EventActions> self(this);
    const QString eventId = event.value(u"eventId"_s).toString();
    m_source->deleteEvent(toEvent(event), wholeSeries, [this, self, eventId](const QString &error) {
        if (!self)
            return;
        finish(eventId, error);
        if (error.isEmpty())
            Q_EMIT removed(eventId);
    });
}

void EventActions::move(const QVariantMap &event, const QDateTime &from, const QDateTime &to,
                        bool wholeSeries)
{
    if (!m_source || m_busy)
        return;
    start();
    const QPointer<EventActions> self(this);
    const QString eventId = event.value(u"eventId"_s).toString();
    m_source->moveEvent(toEvent(event), from, to, wholeSeries,
                        [this, self, eventId](const QString &error) {
                            if (!self)
                                return;
                            finish(eventId, error);
                            if (error.isEmpty())
                                Q_EMIT moved(eventId);
                        });
}

namespace {

QTimeZone zoneNamed(const QString &zone)
{
    const QTimeZone named(zone.toUtf8());
    return named.isValid() ? named : QTimeZone::systemTimeZone();
}

} // namespace

QDateTime EventActions::at(const QDateTime &day, int minutes, const QString &zone)
{
    const QTimeZone tz = zoneNamed(zone);
    return Times::shiftWallClock(QDateTime(day.date(), QTime(0, 0), tz), tz, 0,
                                 qint64(minutes) * 60);
}

QDateTime EventActions::dayOf(const QDateTime &time, const QString &zone)
{
    return time.toTimeZone(zoneNamed(zone)).date().startOfDay();
}

int EventActions::minutesOf(const QDateTime &time, const QString &zone)
{
    const QTime clock = time.toTimeZone(zoneNamed(zone)).time();
    return clock.hour() * 60 + clock.minute();
}

int EventActions::daysBetween(const QDateTime &from, const QDateTime &to)
{
    return int(from.date().daysTo(to.date()));
}

QDateTime EventActions::addDays(const QDateTime &day, int days)
{
    return day.date().addDays(days).startOfDay();
}

QVariantList EventActions::repeatChoices(const QDateTime &day)
{
    QVariantList list;
    for (const QString &choice : Repeat::choices())
        list.append(
            QVariantMap{{u"id"_s, choice}, {u"label"_s, Repeat::describe(choice, day.date())}});
    return list;
}

QString EventActions::repeatChoice(const QStringList &recurrence, const QDateTime &day)
{
    return Repeat::choiceOf(recurrence, day.date());
}

QStringList EventActions::repeatRule(const QString &choice, const QDateTime &day)
{
    return Repeat::rule(choice, day.date());
}

QVariantMap EventActions::customRepeat(const QStringList &recurrence, const QDateTime &day,
                                       const QString &zone)
{
    const std::optional<Repeat::Custom> custom =
        Repeat::custom(recurrence, day.date(), zoneNamed(zone));
    if (!custom)
        return {};
    // The start's own weekday when the rule names none, so switching to
    // weekly shows the day it would save.
    QVariantList weekdays;
    for (int weekday : custom->weekdays)
        weekdays << weekday;
    if (weekdays.isEmpty())
        weekdays << day.date().dayOfWeek();
    return {{u"frequency"_s, custom->frequency},
            {u"interval"_s, custom->interval},
            {u"weekdays"_s, weekdays},
            {u"onWeekday"_s, custom->onWeekday},
            {u"until"_s, custom->until.isValid() ? custom->until.startOfDay() : QDateTime()},
            {u"count"_s, custom->count},
            {u"weekStart"_s, custom->weekStart}};
}

QStringList EventActions::customRule(const QVariantMap &custom, const QDateTime &day, bool allDay,
                                     const QString &zone, const QStringList &previous)
{
    Repeat::Custom c;
    c.frequency = custom.value(u"frequency"_s).toString();
    c.interval = std::max(1, custom.value(u"interval"_s).toInt());
    for (const QVariant &weekday : custom.value(u"weekdays"_s).toList())
        c.weekdays << weekday.toInt();
    c.onWeekday = custom.value(u"onWeekday"_s).toBool();
    c.until = custom.value(u"until"_s).toDateTime().date();
    c.count = std::max(0, custom.value(u"count"_s).toInt());
    c.weekStart = custom.value(u"weekStart"_s).toString();
    QStringList lines = Repeat::rule(c, day.date(), allDay, zoneNamed(zone));
    for (const QString &line : previous) {
        if (!line.startsWith(u"RRULE:"_s, Qt::CaseInsensitive))
            lines << line;
    }
    return lines;
}

QVariantMap EventActions::shiftCustom(const QVariantMap &custom, int days)
{
    Repeat::Custom c;
    for (const QVariant &weekday : custom.value(u"weekdays"_s).toList())
        c.weekdays << weekday.toInt();
    QVariantList weekdays;
    for (int weekday : Repeat::shifted(c, days).weekdays)
        weekdays << weekday;
    QVariantMap shifted = custom;
    shifted.insert(u"weekdays"_s, weekdays);
    return shifted;
}

QUrl EventActions::mapUrl(const QString &app, const QString &location)
{
    return places::mapUrl(app, location);
}

QString EventActions::describeRepeat(const QStringList &recurrence, const QDateTime &day,
                                     const QString &zone)
{
    const QString choice = Repeat::choiceOf(recurrence, day.date());
    if (choice != u"custom")
        return Repeat::describe(choice, day.date());
    if (const auto custom = Repeat::custom(recurrence, day.date(), zoneNamed(zone)))
        return Repeat::describe(*custom, day.date());
    return Repeat::describe(choice, day.date());
}

EventEdit EventActions::toEdit(const QVariantMap &changes)
{
    EventEdit edit;
    const auto has = [&changes](const QString &key) { return changes.contains(key); };
    if (has(u"summary"_s))
        edit.summary = changes.value(u"summary"_s).toString().trimmed();
    if (has(u"location"_s))
        edit.location = changes.value(u"location"_s).toString().trimmed();
    if (has(u"description"_s))
        edit.description = changes.value(u"description"_s).toString();
    // Times arrive in the system zone; they are written in the event's own.
    const QTimeZone zone(changes.value(u"zone"_s).toString().toUtf8());
    const auto inZone = [&zone](const QDateTime &time) {
        return zone.isValid() ? time.toTimeZone(zone) : time;
    };
    // An all-day day comes as midnight where it was picked, here or in the
    // event's zone, and is written as that midnight in the event's zone.
    const bool allDay = changes.value(u"allDay"_s).toBool();
    const auto when = [&](const QString &key) {
        const QDateTime given = changes.value(key).toDateTime();
        const QDateTime time = inZone(given);
        if (!allDay)
            return time;
        const QDate day = given.time() == QTime(0, 0) ? given.date() : time.date();
        return QDateTime(day, QTime(0, 0), time.timeZone());
    };
    if (has(u"start"_s))
        edit.start = when(u"start"_s);
    if (has(u"end"_s))
        edit.end = when(u"end"_s);
    if (has(u"allDay"_s))
        edit.allDay = allDay;
    if (has(u"recurrence"_s))
        edit.recurrence = changes.value(u"recurrence"_s).toStringList();
    if (has(u"guests"_s)) {
        QStringList guests;
        for (const QString &guest : changes.value(u"guests"_s).toStringList()) {
            if (!guest.trimmed().isEmpty())
                guests.append(guest.trimmed());
        }
        edit.guests = guests;
    }
    if (has(u"videoCall"_s))
        edit.videoCall = changes.value(u"videoCall"_s).toBool();
    return edit;
}

void EventActions::update(const QVariantMap &event, const QVariantMap &changes,
                          const QString &scope)
{
    if (!m_source || m_busy)
        return;
    const EventEdit edit = toEdit(changes);
    const QString eventId = event.value(u"eventId"_s).toString();
    if (edit.isEmpty()) {
        Q_EMIT updated(eventId);
        return;
    }
    start();
    const EditScope range = scope == u"all"         ? EditScope::AllEvents
                            : scope == u"following" ? EditScope::ThisAndFollowing
                                                    : EditScope::ThisEvent;
    const QPointer<EventActions> self(this);
    m_source->updateEvent(toEvent(event), edit, range, [this, self, eventId](const QString &error) {
        if (!self)
            return;
        finish(eventId, error);
        if (error.isEmpty())
            Q_EMIT updated(eventId);
    });
}

EventDraft EventActions::toCopy(const QVariantMap &event, const QDateTime &start)
{
    EventDraft draft;
    draft.summary = event.value(u"summary"_s).toString();
    draft.location = event.value(u"location"_s).toString();
    draft.description = event.value(u"description"_s).toString();
    draft.allDay = event.value(u"allDay"_s).toBool();
    draft.calendarId = event.value(u"calendarId"_s).toString();
    draft.start = event.value(u"start"_s).toDateTime();
    draft.end = event.value(u"end"_s).toDateTime();
    if (!start.isValid())
        return draft;
    if (draft.allDay) {
        // Whole days, from midnight on the day picked.
        const qint64 days = draft.start.date().daysTo(draft.end.date());
        draft.start = QDateTime(start.date(), QTime(0, 0), draft.start.timeZone());
        draft.end = QDateTime(start.date().addDays(days), QTime(0, 0), draft.start.timeZone());
    } else {
        draft.end = start.addSecs(draft.start.secsTo(draft.end));
        draft.start = start;
    }
    return draft;
}

void EventActions::duplicate(const QVariantMap &event, const QDateTime &at,
                             const QString &fallbackCalendar)
{
    if (!m_source || m_busy)
        return;
    EventDraft draft = toCopy(event, at);
    const QList<CalendarInfo> calendars = m_source->calendars();
    const auto writable = [&calendars](const QString &id) {
        return std::any_of(calendars.cbegin(), calendars.cend(),
                           [&id](const CalendarInfo &c) { return c.id == id && c.writable; });
    };
    if (!writable(draft.calendarId)) {
        const auto first = std::find_if(calendars.cbegin(), calendars.cend(),
                                        [](const CalendarInfo &c) { return c.writable; });
        draft.calendarId = writable(fallbackCalendar)  ? fallbackCalendar
                           : first != calendars.cend() ? first->id
                                                       : QString();
    }
    const QString eventId = event.value(u"eventId"_s).toString();
    if (draft.calendarId.isEmpty()) {
        finish(eventId, tr("No calendar can take a copy."));
        return;
    }
    start();
    const QPointer<EventActions> self(this);
    m_source->createEvent(draft, [this, self, eventId](const QString &error) {
        if (!self)
            return;
        finish(eventId, error);
        if (error.isEmpty())
            Q_EMIT duplicated();
    });
}

QUrl EventActions::mailGuests(const QVariantMap &event)
{
    const QStringList guests = event.value(u"attendees"_s).toStringList();
    if (guests.isEmpty())
        return {};
    QUrl url;
    url.setScheme(u"mailto"_s);
    url.setPath(guests.join(u','));
    QUrlQuery query;
    query.addQueryItem(u"subject"_s, QString::fromUtf8(QUrl::toPercentEncoding(
                                         event.value(u"summary"_s).toString())));
    url.setQuery(query.query(QUrl::FullyEncoded), QUrl::StrictMode);
    return url;
}

void EventActions::clearError()
{
    if (m_error.isEmpty())
        return;
    m_error.clear();
    m_errorEventId.clear();
    Q_EMIT errorChanged();
}

void EventActions::start()
{
    m_busy = true;
    Q_EMIT busyChanged();
    clearError();
}

void EventActions::finish(const QString &eventId, const QString &error)
{
    m_busy = false;
    Q_EMIT busyChanged();
    if (!error.isEmpty()) {
        m_error = error;
        m_errorEventId = eventId;
        Q_EMIT errorChanged();
    }
}

} // namespace callie
