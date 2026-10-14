#include "callie/GraphEvents.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/RecurrenceRule>

#include <QBitArray>
#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace callie::graph {

namespace {

using KEvent = KCalendarCore::Event;

// Graph's day names, Monday first as KCalendarCore counts them.
const QStringList kDays = {u"monday"_s, u"tuesday"_s,  u"wednesday"_s, u"thursday"_s,
                           u"friday"_s, u"saturday"_s, u"sunday"_s};
const QStringList kIcsDays = {u"MO"_s, u"TU"_s, u"WE"_s, u"TH"_s, u"FR"_s, u"SA"_s, u"SU"_s};
const QStringList kIndexes = {u"first"_s, u"second"_s, u"third"_s, u"fourth"_s};

int indexOf(const QString &index)
{
    return index == u"last" ? -1 : int(kIndexes.indexOf(index)) + 1;
}

QString indexName(int position)
{
    return position == -1 ? u"last"_s : kIndexes.value(position - 1);
}

KCalendarCore::Attendee::PartStat partstatFor(const QString &response)
{
    using A = KCalendarCore::Attendee;
    return response == u"accepted" || response == u"organizer" ? A::Accepted
           : response == u"declined"                           ? A::Declined
           : response == u"tentativelyAccepted"                ? A::Tentative
                                                               : A::NeedsAction;
}

/// The zone a series repeats in: where it was made, else UTC.
QTimeZone zoneOf(const QJsonObject &event)
{
    const QString range = event[u"recurrence"][u"range"][u"recurrenceTimeZone"].toString();
    return zoneFor(range.isEmpty() ? event[u"originalStartTimeZone"].toString() : range);
}

/// One event's own fields: everything but its repeat and its place in a series.
KEvent::Ptr eventFrom(const QJsonObject &json, const QString &uid, const QTimeZone &zone)
{
    KEvent::Ptr event(new KEvent);
    event->setUid(uid);
    event->setSummary(json[u"subject"].toString());
    event->setDescription(json[u"body"][u"content"].toString().trimmed());
    event->setLocation(json[u"location"][u"displayName"].toString());
    const bool allDay = json[u"isAllDay"].toBool();
    event->setAllDay(allDay);
    const QDateTime start = timeOf(json[u"start"].toObject());
    const QDateTime end = timeOf(json[u"end"].toObject());
    if (allDay) {
        // All-day times are midnights where the event was made, wherever they are asked for.
        const auto day = [&zone](const QDateTime &at) {
            const QDateTime local = at.toTimeZone(zone);
            return local.time() == QTime(0, 0) ? local.date() : at.date();
        };
        event->setDtStart(day(start).startOfDay());
        event->setDtEnd(day(end).addDays(-1).startOfDay());
    } else {
        event->setDtStart(start.toTimeZone(zone));
        event->setDtEnd(end.toTimeZone(zone));
    }
    if (json[u"showAs"].toString() == u"free")
        event->setTransparency(KEvent::Transparent);
    if (json[u"isCancelled"].toBool())
        event->setStatus(KCalendarCore::Incidence::StatusCanceled);
    const QJsonObject organizer = json[u"organizer"][u"emailAddress"].toObject();
    if (!organizer[u"address"].toString().isEmpty())
        event->setOrganizer(
            KCalendarCore::Person(organizer[u"name"].toString(), organizer[u"address"].toString()));
    for (const QJsonValue &value : json[u"attendees"].toArray()) {
        const QJsonObject person = value[u"emailAddress"].toObject();
        event->addAttendee(
            KCalendarCore::Attendee(person[u"name"].toString(), person[u"address"].toString(),
                                    false, partstatFor(value[u"status"][u"response"].toString())));
    }
    const QString joinUrl = json[u"onlineMeeting"][u"joinUrl"].toString();
    if (!joinUrl.isEmpty())
        event->addConference(KCalendarCore::Conference(QUrl(joinUrl), u"Microsoft Teams"_s,
                                                       {u"AUDIO"_s, u"VIDEO"_s}));
    const QDateTime modified =
        QDateTime::fromString(json[u"lastModifiedDateTime"].toString(), Qt::ISODate);
    if (modified.isValid())
        event->setLastModified(modified);
    return event;
}

QJsonObject dateTimeOf(const QDateTime &at, const QTimeZone &zone)
{
    const QByteArray windows = zone.isValid() && zone.id() != "UTC"
                                   ? QTimeZone::ianaIdToWindowsId(zone.id())
                                   : QByteArray();
    const QDateTime local = windows.isEmpty() ? at.toUTC() : at.toTimeZone(zone);
    return {{u"dateTime"_s, local.toString(u"yyyy-MM-dd'T'HH:mm:ss"_s)},
            {u"timeZone"_s, windows.isEmpty() ? u"UTC"_s : QString::fromLatin1(windows)}};
}

} // namespace

QTimeZone zoneFor(const QString &name)
{
    if (name.isEmpty() || name.compare(u"UTC", Qt::CaseInsensitive) == 0 ||
        name.startsWith(u"tzone://"))
        return QTimeZone::UTC;
    const QByteArray iana = QTimeZone::windowsIdToDefaultIanaId(name.toUtf8());
    const QTimeZone zone(iana.isEmpty() ? name.toUtf8() : iana);
    return zone.isValid() ? zone : QTimeZone(QTimeZone::UTC);
}

QDateTime timeOf(const QJsonObject &dateTimeTimeZone)
{
    // Graph writes seven digits of fractions, which Qt does not read.
    QDateTime at =
        QDateTime::fromString(dateTimeTimeZone[u"dateTime"].toString().left(19), Qt::ISODate);
    at.setTimeZone(zoneFor(dateTimeTimeZone[u"timeZone"].toString()));
    return at;
}

QString ruleFor(const QJsonObject &recurrence, const QDateTime &start, bool allDay)
{
    const QJsonObject pattern = recurrence[u"pattern"].toObject();
    const QJsonObject range = recurrence[u"range"].toObject();
    const QString type = pattern[u"type"].toString();
    QStringList days;
    for (const QJsonValue &day : pattern[u"daysOfWeek"].toArray()) {
        const qsizetype at = kDays.indexOf(day.toString());
        if (at >= 0)
            days << kIcsDays.at(at);
    }
    const int index = indexOf(pattern[u"index"].toString(u"first"_s));
    // One weekday takes its position, as 2MO; several take a BYSETPOS.
    const auto positioned = [&days, index] {
        return days.size() == 1
                   ? u"BYDAY="_s + QString::number(index) + days.first()
                   : u"BYDAY="_s + days.join(u',') + u";BYSETPOS="_s + QString::number(index);
    };
    QStringList parts;
    if (type == u"daily") {
        parts << u"FREQ=DAILY"_s;
    } else if (type == u"weekly" && !days.isEmpty()) {
        parts << u"FREQ=WEEKLY"_s << u"BYDAY="_s + days.join(u',');
        const qsizetype first = kDays.indexOf(pattern[u"firstDayOfWeek"].toString());
        if (first >= 0)
            parts << u"WKST="_s + kIcsDays.at(first);
    } else if (type == u"absoluteMonthly") {
        parts << u"FREQ=MONTHLY"_s
              << u"BYMONTHDAY="_s +
                     QString::number(pattern[u"dayOfMonth"].toInt(start.date().day()));
    } else if (type == u"relativeMonthly" && !days.isEmpty()) {
        parts << u"FREQ=MONTHLY"_s << positioned();
    } else if (type == u"absoluteYearly") {
        parts << u"FREQ=YEARLY"_s
              << u"BYMONTH="_s + QString::number(pattern[u"month"].toInt(start.date().month()))
              << u"BYMONTHDAY="_s +
                     QString::number(pattern[u"dayOfMonth"].toInt(start.date().day()));
    } else if (type == u"relativeYearly" && !days.isEmpty()) {
        parts << u"FREQ=YEARLY"_s
              << u"BYMONTH="_s + QString::number(pattern[u"month"].toInt(start.date().month()))
              << positioned();
    } else {
        return {};
    }
    const int interval = pattern[u"interval"].toInt(1);
    if (interval > 1)
        parts.insert(1, u"INTERVAL="_s + QString::number(interval));
    const QString end = range[u"type"].toString();
    if (end == u"numbered") {
        parts << u"COUNT="_s + QString::number(range[u"numberOfOccurrences"].toInt());
    } else if (end == u"endDate") {
        // Graph's end date is the last day it may happen on.
        const QDate last = QDate::fromString(range[u"endDate"].toString(), Qt::ISODate);
        parts << u"UNTIL="_s + (allDay ? last.toString(u"yyyyMMdd"_s)
                                       : QDateTime(last, start.time(), start.timeZone())
                                             .toUTC()
                                             .toString(u"yyyyMMdd'T'HHmmss'Z'"_s));
    }
    return u"RRULE:"_s + parts.join(u';');
}

ics::Group toIcs(const Series &series)
{
    const QJsonObject &master = series.event;
    const QTimeZone zone = zoneOf(master);
    // The master's iCalUId names the whole series; occurrences have their own.
    const QString uid = master[u"iCalUId"].toString(master[u"id"].toString());
    ics::Group group;
    const KEvent::Ptr event = eventFrom(master, uid, zone);
    group << event;
    const bool allDay = event->allDay();
    const QJsonObject recurrence = master[u"recurrence"].toObject();
    if (!recurrence.isEmpty()) {
        const QString rule = ruleFor(recurrence, event->dtStart(), allDay);
        auto *parsed = new KCalendarCore::RecurrenceRule;
        if (!rule.isEmpty() && KCalendarCore::ICalFormat().fromString(parsed, rule.mid(6)))
            event->recurrence()->addRRule(parsed);
        else
            delete parsed;
        for (const QString &cancelled : series.cancelled) {
            const QDate day = QDate::fromString(cancelled.section(u'.', -1), Qt::ISODate);
            if (!day.isValid())
                continue;
            if (allDay)
                event->recurrence()->addExDate(day);
            else
                event->recurrence()->addExDateTime(
                    QDateTime(day, event->dtStart().time(), event->dtStart().timeZone()));
        }
    }
    for (const QJsonObject &exception : series.exceptions) {
        const KEvent::Ptr changed = eventFrom(exception, uid, zone);
        const QDateTime original =
            QDateTime::fromString(exception[u"originalStart"].toString(), Qt::ISODate);
        changed->setRecurrenceId(allDay ? original.toTimeZone(zone).date().startOfDay()
                                        : original.toTimeZone(zone));
        group << changed;
    }
    return group;
}

std::optional<QJsonObject> recurrenceFor(const KCalendarCore::Event &event, QString *error)
{
    const KCalendarCore::Recurrence *recurrence = event.recurrence();
    const auto refuse = [error] {
        *error = QObject::tr("Outlook cannot repeat an event that way.");
        return std::nullopt;
    };
    if (recurrence->rRules().size() != 1 || !recurrence->exRules().isEmpty() ||
        !recurrence->rDates().isEmpty() || !recurrence->rDateTimes().isEmpty())
        return refuse();
    const QDate first = event.allDay()
                            ? event.dtStart().date()
                            : event.dtStart().toTimeZone(event.dtStart().timeZone()).date();
    QJsonObject pattern{{u"interval"_s, recurrence->frequency()}};
    const auto weekdays = [](const QBitArray &bits) {
        QJsonArray days;
        for (int i = 0; i < 7; ++i) {
            if (bits.testBit(i))
                days << kDays.at(i);
        }
        return days;
    };
    // All positions alike, as Graph keeps one index for all its days.
    const auto positions = [](const QList<KCalendarCore::RecurrenceRule::WDayPos> &list)
        -> std::optional<std::pair<int, QJsonArray>> {
        if (list.isEmpty())
            return std::nullopt;
        QJsonArray days;
        for (const auto &position : list) {
            if (position.pos() != list.first().pos() ||
                (position.pos() < -1 || position.pos() == 0 || position.pos() > 4))
                return std::nullopt;
            days << kDays.at(position.day() - 1);
        }
        return std::pair{list.first().pos(), days};
    };
    switch (recurrence->recurrenceType()) {
    case KCalendarCore::Recurrence::rDaily: pattern.insert(u"type"_s, u"daily"_s); break;
    case KCalendarCore::Recurrence::rWeekly:
        pattern.insert(u"type"_s, u"weekly"_s);
        pattern.insert(u"daysOfWeek"_s, weekdays(recurrence->days()));
        pattern.insert(u"firstDayOfWeek"_s, kDays.at(recurrence->weekStart() - 1));
        break;
    case KCalendarCore::Recurrence::rMonthlyDay: {
        const QList<int> days = recurrence->monthDays();
        if (days.size() > 1 || (days.size() == 1 && days.first() < 1))
            return refuse();
        pattern.insert(u"type"_s, u"absoluteMonthly"_s);
        pattern.insert(u"dayOfMonth"_s, days.isEmpty() ? first.day() : days.first());
        break;
    }
    case KCalendarCore::Recurrence::rMonthlyPos: {
        const auto found = positions(recurrence->monthPositions());
        if (!found)
            return refuse();
        pattern.insert(u"type"_s, u"relativeMonthly"_s);
        pattern.insert(u"daysOfWeek"_s, found->second);
        pattern.insert(u"index"_s, indexName(found->first));
        break;
    }
    case KCalendarCore::Recurrence::rYearlyMonth: {
        const QList<int> months = recurrence->yearMonths();
        const QList<int> days = recurrence->yearDates();
        if (months.size() > 1 || days.size() > 1 || (days.size() == 1 && days.first() < 1))
            return refuse();
        pattern.insert(u"type"_s, u"absoluteYearly"_s);
        pattern.insert(u"month"_s, months.isEmpty() ? first.month() : months.first());
        pattern.insert(u"dayOfMonth"_s, days.isEmpty() ? first.day() : days.first());
        break;
    }
    case KCalendarCore::Recurrence::rYearlyPos: {
        const QList<int> months = recurrence->yearMonths();
        const auto found = positions(recurrence->yearPositions());
        if (months.size() > 1 || !found)
            return refuse();
        pattern.insert(u"type"_s, u"relativeYearly"_s);
        pattern.insert(u"month"_s, months.isEmpty() ? first.month() : months.first());
        pattern.insert(u"daysOfWeek"_s, found->second);
        pattern.insert(u"index"_s, indexName(found->first));
        break;
    }
    default: return refuse();
    }
    QJsonObject range{{u"startDate"_s, first.toString(Qt::ISODate)}};
    const QByteArray windows = QTimeZone::ianaIdToWindowsId(event.dtStart().timeZone().id());
    if (!event.allDay() && !windows.isEmpty())
        range.insert(u"recurrenceTimeZone"_s, QString::fromLatin1(windows));
    if (recurrence->duration() > 0) {
        range.insert(u"type"_s, u"numbered"_s);
        range.insert(u"numberOfOccurrences"_s, recurrence->duration());
    } else if (recurrence->duration() == 0 && recurrence->endDateTime().isValid()) {
        range.insert(u"type"_s, u"endDate"_s);
        const QDateTime end = recurrence->endDateTime();
        range.insert(
            u"endDate"_s,
            (event.allDay() ? end.date() : end.toTimeZone(event.dtStart().timeZone()).date())
                .toString(Qt::ISODate));
    } else {
        range.insert(u"type"_s, u"noEnd"_s);
    }
    return QJsonObject{{u"pattern"_s, pattern}, {u"range"_s, range}};
}

std::optional<QJsonObject> fieldsFor(const KCalendarCore::Event &event, QString *error)
{
    QJsonObject fields{
        {u"subject"_s, event.summary()},
        {u"body"_s,
         QJsonObject{{u"contentType"_s, u"text"_s}, {u"content"_s, event.description()}}},
        {u"location"_s, QJsonObject{{u"displayName"_s, event.location()}}},
        {u"isAllDay"_s, event.allDay()},
        {u"showAs"_s, event.transparency() == KEvent::Transparent ? u"free"_s : u"busy"_s}};
    const QTimeZone zone = event.dtStart().timeSpec() == Qt::TimeZone ? event.dtStart().timeZone()
                                                                      : QTimeZone(QTimeZone::UTC);
    if (event.allDay()) {
        // Midnights, the end the day after the last, as Outlook keeps all-day events.
        const QDate last = event.hasEndDate() ? event.dtEnd().date() : event.dtStart().date();
        fields.insert(u"start"_s,
                      QJsonObject{{u"dateTime"_s, event.dtStart().date().startOfDay().toString(
                                                      u"yyyy-MM-dd'T'HH:mm:ss"_s)},
                                  {u"timeZone"_s, u"UTC"_s}});
        fields.insert(u"end"_s, QJsonObject{{u"dateTime"_s, last.addDays(1).startOfDay().toString(
                                                                u"yyyy-MM-dd'T'HH:mm:ss"_s)},
                                            {u"timeZone"_s, u"UTC"_s}});
    } else {
        fields.insert(u"start"_s, dateTimeOf(event.dtStart(), zone));
        fields.insert(u"end"_s,
                      dateTimeOf(event.hasEndDate() ? event.dtEnd() : event.dtStart(), zone));
    }
    QJsonArray attendees;
    const QString organizer = event.organizer().email();
    for (const KCalendarCore::Attendee &attendee : event.attendees()) {
        if (attendee.email().isEmpty() ||
            attendee.email().compare(organizer, Qt::CaseInsensitive) == 0)
            continue;
        attendees << QJsonObject{{u"type"_s, u"required"_s},
                                 {u"emailAddress"_s, QJsonObject{{u"address"_s, attendee.email()},
                                                                 {u"name"_s, attendee.name()}}}};
    }
    fields.insert(u"attendees"_s, attendees);
    if (event.recurs() && !event.hasRecurrenceId()) {
        const auto recurrence = recurrenceFor(event, error);
        if (!recurrence)
            return std::nullopt;
        fields.insert(u"recurrence"_s, *recurrence);
    }
    return fields;
}

} // namespace callie::graph
