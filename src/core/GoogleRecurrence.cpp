#include "callie/GoogleRecurrence.h"

#include "callie/Logging.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Recurrence>
#include <KCalendarCore/RecurrenceRule>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

/// Where an occurrence of a series originally started, for matching it with
/// the exception that cancels or moves it.
QString occurrenceKey(const QString &seriesId, const GoogleEventTime &time)
{
    return seriesId + u'|' +
           (time.isAllDay() ? time.date.toString(Qt::ISODate)
                            : time.dateTime.toUTC().toString(Qt::ISODate));
}

/// Google's id for one occurrence of a series: the series id and the
/// occurrence's original start, as a UTC basic time or a date.
QString instanceId(const QString &seriesId, const QDateTime &start, bool allDay)
{
    return seriesId + u'_' +
           (allDay ? start.date().toString(u"yyyyMMdd"_s)
                   : start.toUTC().toString(u"yyyyMMdd'T'HHmmss'Z'"_s));
}

QString occurrenceKey(const QString &seriesId, const QDateTime &start, bool allDay)
{
    return seriesId + u'|' +
           (allDay ? start.date().toString(Qt::ISODate) : start.toUTC().toString(Qt::ISODate));
}

/// Parses the values of an RDATE or EXDATE line, with its TZID or VALUE=DATE
/// parameter, into the recurrence.
void addDates(KCalendarCore::Recurrence &recurrence, const QString &line, bool exclude,
              const QTimeZone &seriesZone)
{
    const qsizetype colon = line.indexOf(u':');
    if (colon < 0)
        return;
    const QStringList params = line.left(colon).split(u';').mid(1);
    QTimeZone zone = seriesZone;
    bool dateOnly = false;
    for (const QString &param : params) {
        if (param.startsWith(QLatin1String("TZID=")))
            zone = QTimeZone(param.mid(5).toUtf8());
        else if (param == QLatin1String("VALUE=DATE"))
            dateOnly = true;
    }

    for (const QString &value : line.mid(colon + 1).split(u',')) {
        if (dateOnly || value.size() == 8) {
            const QDate date = QDate::fromString(value, QStringLiteral("yyyyMMdd"));
            exclude ? recurrence.addExDate(date) : recurrence.addRDate(date);
            continue;
        }
        const bool utc = value.endsWith(u'Z');
        QDateTime dateTime = QDateTime::fromString(utc ? value.chopped(1) : value,
                                                   QStringLiteral("yyyyMMdd'T'HHmmss"));
        dateTime.setTimeZone(utc || !zone.isValid() ? QTimeZone::UTC : zone);
        exclude ? recurrence.addExDateTime(dateTime) : recurrence.addRDateTime(dateTime);
    }
}

/// Builds the series' recurrence. Returns false when no line could be used.
bool buildRecurrence(KCalendarCore::Recurrence &recurrence, const GoogleEvent &series,
                     const QDateTime &start, bool allDay)
{
    KCalendarCore::ICalFormat format;
    bool usable = false;
    for (const QString &line : series.recurrence) {
        const qsizetype colon = line.indexOf(u':');
        const QString name = line.left(colon).section(u';', 0, 0).toUpper();
        if (name == QLatin1String("RRULE") || name == QLatin1String("EXRULE")) {
            auto *rule = new KCalendarCore::RecurrenceRule;
            if (!format.fromString(rule, line.mid(colon + 1))) {
                qCInfo(lcSync) << "skipping unreadable recurrence rule in" << series.id;
                delete rule;
                continue;
            }
            name == QLatin1String("RRULE") ? recurrence.addRRule(rule) : recurrence.addExRule(rule);
            usable = usable || name == QLatin1String("RRULE");
        } else if (name == QLatin1String("RDATE") || name == QLatin1String("EXDATE")) {
            addDates(recurrence, line, name == QLatin1String("EXDATE"), start.timeZone());
            usable = usable || name == QLatin1String("RDATE");
        }
    }
    // Rules take their start from the recurrence, so it is set last.
    recurrence.setStartDateTime(start, allDay);
    return usable;
}

Event toEvent(const GoogleEvent &source, const QTimeZone &viewZone)
{
    Event event;
    event.uid = source.recurringEventId.isEmpty() ? source.id : source.recurringEventId;
    event.summary = source.summary;
    event.description = source.description;
    event.location = source.location;
    event.conferenceUrl = source.conferenceUrl;
    event.declined = source.responseStatus == u"declined";
    event.responseStatus = source.responseStatus;
    event.eventId = source.id;
    event.seriesId = source.recurringEventId;
    bool invited = false;
    for (const QJsonValue &attendee : QJsonDocument::fromJson(source.attendees).array()) {
        // Rooms and other resources answer for themselves and are not people.
        if (attendee[u"resource"].toBool())
            continue;
        const Guest guest{
            .email = attendee[u"email"].toString(),
            .name = attendee[u"displayName"].toString(),
            .response = attendee[u"responseStatus"].toString(u"needsAction"_s),
            .organizer = attendee[u"organizer"].toBool(),
            .self = attendee[u"self"].toBool(),
        };
        if (guest.self)
            invited = true;
        else if (!guest.email.isEmpty())
            event.attendees.append(guest.email);
        if (guest.organizer)
            event.guests.prepend(guest);
        else
            event.guests.append(guest);
    }
    // Calendar access is checked by the source; this is what the event allows.
    event.canEdit = source.attendees.isEmpty() || source.organizerSelf || source.guestsCanModify;
    event.canRespond = invited && !source.organizerSelf;
    event.reminders = source.reminders;
    event.remindersKnown = true;
    event.allDay = source.start.isAllDay();
    if (event.allDay) {
        event.start = QDateTime(source.start.date, QTime(0, 0), viewZone);
        // Google's end date is exclusive; a missing one means a single day.
        const QDate end =
            source.end.date.isValid() ? source.end.date : source.start.date.addDays(1);
        event.end = QDateTime(end, QTime(0, 0), viewZone);
    } else {
        event.start = source.start.dateTime;
        event.end = source.end.dateTime.isValid() ? source.end.dateTime : source.start.dateTime;
    }
    if (!source.recurringEventId.isEmpty()) {
        event.recurrenceId = source.originalStart.isAllDay()
                                 ? QDateTime(source.originalStart.date, QTime(0, 0), viewZone)
                                 : source.originalStart.dateTime;
    }
    return event;
}

bool overlaps(const Event &event, const QDateTime &from, const QDateTime &to)
{
    if (event.start == event.end)
        return event.start >= from && event.start < to;
    return event.start < to && event.end > from;
}

} // namespace

QList<Event> expandGoogleEvents(const QList<GoogleEvent> &events, const QDateTime &from,
                                const QDateTime &to, const QTimeZone &viewZone)
{
    QSet<QString> replaced;
    for (const GoogleEvent &event : events) {
        if (!event.recurringEventId.isEmpty())
            replaced.insert(occurrenceKey(event.recurringEventId, event.originalStart));
    }

    QList<Event> result;
    for (const GoogleEvent &source : events) {
        if (source.isCancelled())
            continue;
        const Event base = toEvent(source, viewZone);
        if (!base.start.isValid())
            continue;

        if (source.recurrence.isEmpty() || !source.recurringEventId.isEmpty()) {
            if (overlaps(base, from, to))
                result.append(base);
            continue;
        }

        // All-day series recur on dates, so they expand in the view's zone;
        // timed series expand in the organizer's zone so DST shifts follow it.
        const QDateTime seriesStart = base.allDay ? base.start : source.start.dateTime;
        KCalendarCore::Recurrence recurrence;
        if (!buildRecurrence(recurrence, source, seriesStart, base.allDay)) {
            if (overlaps(base, from, to))
                result.append(base);
            continue;
        }

        const qint64 duration = base.start.secsTo(base.end);
        const QList<QDateTime> starts = recurrence.timesInInterval(from.addSecs(-duration), to);
        for (const QDateTime &start : starts) {
            if (replaced.contains(occurrenceKey(source.id, start, base.allDay)))
                continue;
            Event occurrence = base;
            occurrence.start = base.allDay ? QDateTime(start.date(), QTime(0, 0), viewZone) : start;
            occurrence.end = base.allDay
                                 ? QDateTime(start.date().addDays(base.start.daysTo(base.end)),
                                             QTime(0, 0), viewZone)
                                 : start.addSecs(duration);
            occurrence.recurrenceId = occurrence.start;
            occurrence.seriesId = source.id;
            occurrence.eventId = instanceId(source.id, start, base.allDay);
            if (overlaps(occurrence, from, to))
                result.append(occurrence);
        }
    }
    return result;
}

} // namespace callie
