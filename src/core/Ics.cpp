#include "callie/Ics.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/RecurrenceRule>

#include <QHash>
#include <QSet>

using namespace Qt::StringLiterals;

namespace callie::ics {

namespace {

using KEvent = KCalendarCore::Event;

constexpr qint64 kDaySecs = 24 * 60 * 60;

KEvent::Ptr master(const Group &group)
{
    for (const KEvent::Ptr &event : group) {
        if (!event->hasRecurrenceId())
            return event;
    }
    return {};
}

bool recurs(const Group &group)
{
    const KEvent::Ptr series = master(group);
    return series && series->recurs();
}

Group copied(const Group &group)
{
    Group copy;
    for (const KEvent::Ptr &event : group)
        copy.append(KEvent::Ptr(event->clone()));
    return copy;
}

/// An occurrence's original start, as a series names it: the day for an
/// all-day series, else the moment.
QString key(const QDateTime &start, bool allDay)
{
    return allDay ? start.date().toString(Qt::ISODate) : start.toUTC().toString(Qt::ISODate);
}

/// How many days an all-day event takes, counting its first.
qint64 days(const KEvent &event)
{
    if (!event.hasEndDate())
        return 1;
    return std::max<qint64>(1, event.dtStart().date().daysTo(event.dateEnd()) + 1);
}

/// How long a timed event lasts.
qint64 seconds(const KEvent &event)
{
    if (event.hasEndDate())
        return std::max<qint64>(0, event.dtStart().secsTo(event.dtEnd()));
    if (event.hasDuration())
        return event.duration().asSeconds();
    return 0;
}

QString response(KCalendarCore::Attendee::PartStat status)
{
    switch (status) {
    case KCalendarCore::Attendee::Accepted: return u"accepted"_s;
    case KCalendarCore::Attendee::Declined: return u"declined"_s;
    case KCalendarCore::Attendee::Tentative: return u"tentative"_s;
    default: return u"needsAction"_s;
    }
}

bool sameAddress(const QString &a, const QString &b)
{
    return !a.isEmpty() && a.compare(b, Qt::CaseInsensitive) == 0;
}

QStringList recurrenceLines(const KEvent &series)
{
    QStringList lines;
    if (!series.recurs())
        return lines;
    KCalendarCore::ICalFormat format;
    for (KCalendarCore::RecurrenceRule *rule : series.recurrence()->rRules())
        lines << format.toString(rule).trimmed(); // "RRULE:..." with its line ending
    return lines;
}

/// One occurrence as Callie shows it: `start` is where it starts, in the
/// event's own zone, and `recurrenceId` the start its series gave it.
Event toEvent(const KEvent &source, const KEvent *series, const QDateTime &start,
              const QDateTime &recurrenceId, const Calendar &calendar, const QTimeZone &viewZone)
{
    Event event;
    event.uid = source.uid();
    event.calendarId = calendar.id;
    event.summary = source.summary();
    event.description = source.description();
    event.location = source.location();
    event.allDay = source.allDay();
    if (event.allDay) {
        event.start = QDateTime(start.date(), QTime(0, 0), viewZone);
        event.end = QDateTime(start.date().addDays(days(source)), QTime(0, 0), viewZone);
    } else {
        event.start = start.toTimeZone(viewZone);
        event.end = start.addSecs(seconds(source)).toTimeZone(viewZone);
    }
    if (source.dtStart().timeSpec() == Qt::TimeZone)
        event.zone = QString::fromUtf8(source.dtStart().timeZone().id());
    const QColor own = QColor::fromString(source.color());
    event.color = own.isValid() ? own : calendar.color;
    event.canEdit = calendar.writable;
    if (series)
        event.recurrence = recurrenceLines(*series);
    if (recurrenceId.isValid()) {
        event.recurrenceId = recurrenceId;
        event.seriesId = source.uid();
        event.eventId = source.uid() + u'|' + key(recurrenceId, event.allDay);
        event.detached = source.hasRecurrenceId();
    } else {
        event.eventId = source.uid();
    }
    if (!source.conferences().isEmpty())
        event.conferenceUrl = source.conferences().first().uri();

    const KCalendarCore::Person organizer = source.organizer();
    bool organizerListed = false;
    for (const KCalendarCore::Attendee &attendee : source.attendees()) {
        const Guest guest{
            .email = attendee.email(),
            .name = attendee.name(),
            .response = response(attendee.status()),
            .organizer = sameAddress(attendee.email(), organizer.email()),
            .self = sameAddress(attendee.email(), calendar.self),
        };
        organizerListed = organizerListed || guest.organizer;
        if (guest.self) {
            event.responseStatus = guest.response;
            event.declined = guest.response == u"declined";
            event.canRespond = calendar.writable && !guest.organizer;
        } else {
            event.attendees << guest.email;
        }
        guest.organizer ? event.guests.prepend(guest) : event.guests.append(guest);
    }
    if (!event.guests.isEmpty() && !organizerListed && !organizer.email().isEmpty()) {
        event.guests.prepend({.email = organizer.email(),
                              .name = organizer.name(),
                              .response = u"accepted"_s,
                              .organizer = true,
                              .self = sameAddress(organizer.email(), calendar.self)});
    }

    // The file says what to be reminded of, so no alarm means no reminder.
    event.remindersKnown = true;
    for (const KCalendarCore::Alarm::Ptr &alarm : source.alarms()) {
        if (alarm->enabled() && alarm->hasStartOffset() && alarm->startOffset().asSeconds() <= 0)
            event.reminders << int(-alarm->startOffset().asSeconds() / 60);
    }
    return event;
}

bool overlaps(const Event &event, const QDateTime &from, const QDateTime &to)
{
    if (event.start == event.end)
        return event.start >= from && event.start < to;
    return event.start < to && event.end > from;
}

/// Puts `start` in the zone `like` is written in, so a series keeps its clock.
QDateTime inZoneOf(const QDateTime &start, const QDateTime &like)
{
    switch (like.timeSpec()) {
    case Qt::TimeZone: return start.toTimeZone(like.timeZone());
    case Qt::UTC: return start.toUTC();
    case Qt::LocalTime: return start.toLocalTime();
    default: return start;
    }
}

/// How far a move from one start to another goes on the series' own clock:
/// days, then time of day, so it keeps its hour across a change of clocks.
struct Shift
{
    int days = 0;
    qint64 seconds = 0;

    [[nodiscard]] QDateTime apply(const QDateTime &time) const
    {
        return time.addDays(days).addSecs(seconds);
    }
};

Shift shiftBetween(const QDateTime &from, const QDateTime &to, const QDateTime &like, bool allDay)
{
    if (allDay)
        return {int(from.date().daysTo(to.date())), 0};
    const QDateTime a = inZoneOf(from, like);
    const QDateTime b = inZoneOf(to, like);
    return {int(a.date().daysTo(b.date())), a.time().secsTo(b.time())};
}

/// Gives `event` new times. All-day ends are the day after the last, as in
/// Callie's events; KCalendarCore keeps the last day itself.
void setTimes(KEvent &event, const QDateTime &start, const QDateTime &end, bool allDay)
{
    if (allDay) {
        const QDate last = end.date() > start.date() && end.time() == QTime(0, 0)
                               ? end.date().addDays(-1)
                               : std::max(start.date(), end.date());
        event.setDtStart(QDateTime(start.date(), QTime(0, 0)));
        event.setAllDay(true);
        event.setDtEnd(QDateTime(last, QTime(0, 0)));
        return;
    }
    event.setAllDay(false);
    event.setDtStart(inZoneOf(start, event.dtStart()));
    event.setDtEnd(inZoneOf(end, event.dtStart()));
}

/// The occurrence's own copy in `group`, made from its series if it has none.
KEvent::Ptr ownCopy(Group &group, const Event &occurrence)
{
    const KEvent::Ptr series = master(group);
    for (const KEvent::Ptr &event : std::as_const(group)) {
        if (event->hasRecurrenceId() && key(event->recurrenceId(), event->allDay()) ==
                                            key(occurrence.recurrenceId, occurrence.allDay))
            return event;
    }
    KEvent::Ptr copy(series->clone());
    copy->clearRecurrence();
    const QDateTime original = series->allDay()
                                   ? QDateTime(occurrence.recurrenceId.date(), QTime(0, 0))
                                   : inZoneOf(occurrence.recurrenceId, series->dtStart());
    copy->setRecurrenceId(original);
    if (series->allDay()) {
        copy->setDtStart(original);
        copy->setDtEnd(original.addDays(days(*series) - 1));
    } else {
        copy->setDtStart(original);
        copy->setDtEnd(original.addSecs(seconds(*series)));
    }
    group.append(copy);
    return copy;
}

/// Moves a whole series, with what is kept about its occurrences, as one of
/// them moves from `from` to `to`, giving each occurrence `length` seconds.
void shiftSeries(Group &group, const QDateTime &from, const QDateTime &to, qint64 length,
                 bool allDay)
{
    const KEvent::Ptr series = master(group);
    const Shift shift = shiftBetween(from, to, series->dtStart(), allDay);
    const QDateTime start = shift.apply(series->dtStart());
    if (series->recurs()) {
        QList<QDateTime> excluded;
        for (const QDateTime &date : series->recurrence()->exDateTimes())
            excluded << shift.apply(date);
        series->recurrence()->setExDateTimes(excluded);
        KCalendarCore::DateList excludedDays;
        for (const QDate &date : series->recurrence()->exDates())
            excludedDays << date.addDays(shift.days);
        series->recurrence()->setExDates(excludedDays);
    }
    // All-day lengths in whole days, which a change of clocks makes 23 or 25 hours.
    const QDateTime end = allDay ? start.addDays(std::max<qint64>(1, qRound(length / 86400.0)))
                                 : start.addSecs(length);
    setTimes(*series, start, end, allDay);
    for (const KEvent::Ptr &event : std::as_const(group)) {
        if (event->hasRecurrenceId())
            event->setRecurrenceId(shift.apply(event->recurrenceId()));
    }
}

bool setRecurrence(KEvent &series, const QStringList &lines, QString *error)
{
    if (lines.isEmpty()) {
        series.clearRecurrence();
        return true;
    }
    KCalendarCore::Recurrence *recurrence = series.recurrence();
    for (KCalendarCore::RecurrenceRule *rule : recurrence->rRules())
        recurrence->deleteRRule(rule);
    KCalendarCore::ICalFormat format;
    for (const QString &line : lines) {
        if (!line.startsWith(u"RRULE:"_s, Qt::CaseInsensitive))
            continue;
        auto *rule = new KCalendarCore::RecurrenceRule;
        if (!format.fromString(rule, line.mid(6))) {
            delete rule;
            *error = QObject::tr("Callie could not read the repeat %1.").arg(line);
            return false;
        }
        recurrence->addRRule(rule);
    }
    recurrence->setStartDateTime(series.dtStart(), series.allDay());
    return true;
}

/// Replaces the guests other than `self`, keeping the answers of those who stay.
void setGuests(KEvent &event, const QStringList &emails)
{
    const KCalendarCore::Attendee::List before = event.attendees();
    KCalendarCore::Attendee::List after;
    for (const KCalendarCore::Attendee &attendee : before) {
        if (sameAddress(attendee.email(), event.organizer().email()))
            after << attendee;
    }
    for (const QString &email : emails) {
        const auto kept = std::ranges::find_if(before, [&email](const auto &attendee) {
            return sameAddress(attendee.email(), email);
        });
        if (kept == before.end())
            after << KCalendarCore::Attendee(QString(), email, true,
                                             KCalendarCore::Attendee::NeedsAction);
        else if (!sameAddress(kept->email(), event.organizer().email()))
            after << *kept;
    }
    event.setAttendees(after);
}

/// What an edit says besides times and repeats.
bool applyDetails(KEvent &event, const EventEdit &edit, QString *error)
{
    if (edit.videoCall && *edit.videoCall != !event.conferences().isEmpty()) {
        *error = QObject::tr("This calendar cannot add video calls.");
        return false;
    }
    if (edit.summary)
        event.setSummary(*edit.summary);
    if (edit.location)
        event.setLocation(*edit.location);
    if (edit.description)
        event.setDescription(*edit.description);
    if (edit.guests)
        setGuests(event, *edit.guests);
    return true;
}

/// The times an edit gives an occurrence.
struct Times
{
    QDateTime start;
    QDateTime end;
    bool allDay;
};

Times timesAfter(const Event &occurrence, const EventEdit &edit)
{
    return {edit.start.value_or(occurrence.start), edit.end.value_or(occurrence.end),
            edit.allDay.value_or(occurrence.allDay)};
}

/// Edits every occurrence: details on the series, and times moved by as much
/// as `occurrence` moves.
bool editSeries(Group &group, const Event &occurrence, const EventEdit &edit, QString *error)
{
    const KEvent::Ptr series = master(group);
    if (!applyDetails(*series, edit, error))
        return false;
    if (edit.movesTimes()) {
        const Times times = timesAfter(occurrence, edit);
        if (times.allDay != series->allDay()) {
            const QDateTime first = series->dtStart();
            const QDateTime start =
                times.allDay ? QDateTime(first.date(), QTime(0, 0))
                             : QDateTime(first.date(), times.start.time(), times.start.timeZone());
            setTimes(*series, start, start.addSecs(times.start.secsTo(times.end)), times.allDay);
        } else {
            shiftSeries(group, occurrence.start, times.start, times.start.secsTo(times.end),
                        times.allDay);
        }
    }
    if (edit.recurrence && !setRecurrence(*series, *edit.recurrence, error))
        return false;
    if (edit.recurrence && edit.recurrence->isEmpty())
        group = {series};
    return true;
}

} // namespace

Group parse(QByteArrayView text, QString *error)
{
    const auto calendar = KCalendarCore::MemoryCalendar::Ptr::create(QTimeZone::UTC);
    KCalendarCore::ICalFormat format;
    if (!format.fromRawString(calendar, text.toByteArray())) {
        if (error)
            *error = QObject::tr("not an iCalendar file");
        return {};
    }
    Group events;
    for (const KEvent::Ptr &event : calendar->rawEvents())
        events.append(KEvent::Ptr(event->clone()));
    return events;
}

QByteArray write(const Group &events)
{
    const auto calendar = KCalendarCore::MemoryCalendar::Ptr::create(QTimeZone::UTC);
    for (const KEvent::Ptr &event : events)
        calendar->addEvent(KEvent::Ptr(event->clone()));
    KCalendarCore::ICalFormat format;
    return format.toString(calendar).toUtf8();
}

QByteArray replaced(QByteArrayView file, const QString &uid, const Group &events)
{
    const auto calendar = KCalendarCore::MemoryCalendar::Ptr::create(QTimeZone::UTC);
    KCalendarCore::ICalFormat format;
    if (!file.isEmpty())
        (void)format.fromRawString(calendar, file.toByteArray());
    for (const KCalendarCore::Incidence::Ptr &incidence : calendar->rawIncidences()) {
        if (incidence->uid() == uid)
            calendar->deleteIncidence(incidence);
    }
    for (const KEvent::Ptr &event : events)
        calendar->addEvent(KEvent::Ptr(event->clone()));
    if (calendar->rawIncidences().isEmpty())
        return {};
    return format.toString(calendar).toUtf8();
}

QList<Group> byUid(const Group &events)
{
    QList<Group> groups;
    QHash<QString, qsizetype> index;
    for (const KEvent::Ptr &event : events) {
        const auto found = index.constFind(event->uid());
        if (found == index.constEnd()) {
            index.insert(event->uid(), groups.size());
            groups.append({event});
        } else {
            groups[*found].append(event);
        }
    }
    return groups;
}

QList<Event> expand(const Group &group, const Calendar &calendar, const QDateTime &from,
                    const QDateTime &to, const QTimeZone &viewZone)
{
    QList<Event> found;
    const KEvent::Ptr series = master(group);
    QSet<QString> changed;
    for (const KEvent::Ptr &event : group) {
        if (!event->hasRecurrenceId())
            continue;
        changed.insert(key(event->recurrenceId(), event->allDay()));
        if (event->status() == KCalendarCore::Incidence::StatusCanceled)
            continue;
        const Event occurrence = toEvent(*event, series.get(), event->dtStart(),
                                         event->recurrenceId(), calendar, viewZone);
        if (overlaps(occurrence, from, to))
            found << occurrence;
    }
    if (!series || series->status() == KCalendarCore::Incidence::StatusCanceled)
        return found;
    if (!series->recurs()) {
        const Event event = toEvent(*series, nullptr, series->dtStart(), {}, calendar, viewZone);
        if (overlaps(event, from, to))
            found << event;
        return found;
    }
    // Wide enough to catch an occurrence that started before `from`, or an
    // all-day one whose day starts earlier in the series' zone.
    const qint64 length = series->allDay() ? days(*series) * kDaySecs : seconds(*series);
    const QList<QDateTime> starts = series->recurrence()->timesInInterval(
        from.addSecs(-length - kDaySecs), to.addSecs(kDaySecs));
    for (const QDateTime &start : starts) {
        if (changed.contains(key(start, series->allDay())))
            continue;
        const Event event = toEvent(*series, series.get(), start, start, calendar, viewZone);
        if (overlaps(event, from, to))
            found << event;
    }
    return found;
}

KCalendarCore::Event::Ptr fromDraft(const EventDraft &draft, const QString &uid)
{
    KEvent::Ptr event(new KEvent);
    event->setUid(uid);
    event->setSummary(draft.summary);
    event->setLocation(draft.location);
    event->setDescription(draft.description);
    event->setDtStart(draft.start);
    setTimes(*event, draft.start, draft.end, draft.allDay);
    return event;
}

std::optional<Changed> remove(const Group &group, const Event &occurrence, bool wholeSeries,
                              QString *error)
{
    Q_UNUSED(error)
    if (wholeSeries || !recurs(group))
        return Changed{};
    Group after = copied(group);
    const QString gone = key(occurrence.recurrenceId, occurrence.allDay);
    after.removeIf([&gone](const KEvent::Ptr &event) {
        return event->hasRecurrenceId() && key(event->recurrenceId(), event->allDay()) == gone;
    });
    const KEvent::Ptr series = master(after);
    if (series->allDay())
        series->recurrence()->addExDate(occurrence.recurrenceId.date());
    else
        series->recurrence()->addExDateTime(inZoneOf(occurrence.recurrenceId, series->dtStart()));
    return Changed{after};
}

std::optional<Changed> move(const Group &group, const Event &occurrence, const QDateTime &start,
                            const QDateTime &end, bool wholeSeries, QString *error)
{
    Group after = copied(group);
    const KEvent::Ptr series = master(after);
    if (!series) {
        *error = QObject::tr("That event is not in this calendar any more.");
        return std::nullopt;
    }
    if (!series->recurs()) {
        setTimes(*series, start, end, occurrence.allDay);
    } else if (wholeSeries) {
        shiftSeries(after, occurrence.start, start, start.secsTo(end), occurrence.allDay);
    } else {
        setTimes(*ownCopy(after, occurrence), start, end, occurrence.allDay);
    }
    return Changed{after};
}

std::optional<Changed> update(const Group &group, const Event &occurrence, const EventEdit &edit,
                              EditScope scope, const QString &newUid, QString *error)
{
    Group after = copied(group);
    const KEvent::Ptr series = master(after);
    if (!series) {
        *error = QObject::tr("That event is not in this calendar any more.");
        return std::nullopt;
    }
    if (!series->recurs() || scope == EditScope::AllEvents ||
        (scope == EditScope::ThisAndFollowing && key(occurrence.recurrenceId, occurrence.allDay) ==
                                                     key(series->dtStart(), series->allDay()))) {
        if (!series->recurs() && !edit.recurrence) {
            if (!applyDetails(*series, edit, error))
                return std::nullopt;
            if (edit.movesTimes()) {
                const Times times = timesAfter(occurrence, edit);
                setTimes(*series, times.start, times.end, times.allDay);
            }
            return Changed{after};
        }
        if (!editSeries(after, occurrence, edit, error))
            return std::nullopt;
        return Changed{after};
    }
    if (scope == EditScope::ThisEvent) {
        const KEvent::Ptr own = ownCopy(after, occurrence);
        if (!applyDetails(*own, edit, error))
            return std::nullopt;
        if (edit.movesTimes()) {
            const Times times = timesAfter(occurrence, edit);
            setTimes(*own, times.start, times.end, times.allDay);
        }
        return Changed{after};
    }

    // This and following: the series ends before the occurrence, and a new
    // one carries on from it with the change made.
    const QDateTime at = series->allDay() ? QDateTime(occurrence.recurrenceId.date(), QTime(0, 0))
                                          : inZoneOf(occurrence.recurrenceId, series->dtStart());
    Group rest{KEvent::Ptr(series->clone())};
    const KEvent::Ptr next = rest.first();
    next->setUid(newUid);
    const int before =
        int(series->recurrence()->timesInInterval(series->dtStart(), at.addSecs(-1)).size());
    for (KCalendarCore::RecurrenceRule *rule : series->recurrence()->rRules()) {
        if (rule->duration() > 0)
            rule->setDuration(before);
        else
            rule->setEndDt(series->allDay() ? at.addDays(-1) : at.addSecs(-1));
    }
    for (KCalendarCore::RecurrenceRule *rule : next->recurrence()->rRules()) {
        if (rule->duration() > 0)
            rule->setDuration(std::max(1, rule->duration() - before));
    }
    setTimes(*next, at, series->allDay() ? at.addDays(days(*series)) : at.addSecs(seconds(*series)),
             series->allDay());
    next->recurrence()->setStartDateTime(next->dtStart(), next->allDay());
    // Occurrences changed on their own go with the series they now fall in.
    const QString split = key(at, series->allDay());
    for (const KEvent::Ptr &event : std::as_const(after)) {
        if (!event->hasRecurrenceId() || key(event->recurrenceId(), event->allDay()) < split)
            continue;
        KEvent::Ptr moved(event->clone());
        moved->setUid(newUid);
        rest.append(moved);
    }
    after.removeIf([&split](const KEvent::Ptr &event) {
        return event->hasRecurrenceId() && key(event->recurrenceId(), event->allDay()) >= split;
    });
    Event first = occurrence;
    first.uid = newUid;
    first.detached = false;
    if (!editSeries(rest, first, edit, error))
        return std::nullopt;
    return Changed{after, rest};
}

std::optional<Changed> respond(const Group &group, const Event &occurrence, const QString &status,
                               bool wholeSeries, const QString &self, QString *error)
{
    static const QHash<QString, KCalendarCore::Attendee::PartStat> answers{
        {u"accepted"_s, KCalendarCore::Attendee::Accepted},
        {u"tentative"_s, KCalendarCore::Attendee::Tentative},
        {u"declined"_s, KCalendarCore::Attendee::Declined},
    };
    Group after = copied(group);
    KEvent::Ptr target = master(after);
    if (!target) {
        *error = QObject::tr("That event is not in this calendar any more.");
        return std::nullopt;
    }
    if (!wholeSeries && target->recurs())
        target = ownCopy(after, occurrence);
    KCalendarCore::Attendee::List attendees = target->attendees();
    const auto you = std::ranges::find_if(
        attendees, [&self](const auto &attendee) { return sameAddress(attendee.email(), self); });
    if (you == attendees.end() || !answers.contains(status)) {
        *error = QObject::tr("You are not invited to this event.");
        return std::nullopt;
    }
    you->setStatus(answers.value(status));
    you->setRSVP(false);
    target->setAttendees(attendees);
    return Changed{after};
}

} // namespace callie::ics
