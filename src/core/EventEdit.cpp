#include "callie/EventEdit.h"

#include "callie/Event.h"

#include <QTimeZone>

#include <algorithm>

namespace callie {

void applyEdit(Event &occurrence, const Event &edited, const EventEdit &edit)
{
    if (edit.summary)
        occurrence.summary = *edit.summary;
    if (edit.location)
        occurrence.location = *edit.location;
    if (edit.description)
        occurrence.description = *edit.description;
    if (edit.recurrence)
        occurrence.recurrence = *edit.recurrence;
    if (edit.guests) {
        occurrence.attendees = *edit.guests;
        // Those who stay keep their answers; the user stays; the new have none yet.
        QList<Guest> guests;
        for (const Guest &guest : std::as_const(occurrence.guests)) {
            if (guest.self || edit.guests->contains(guest.email, Qt::CaseInsensitive))
                guests.append(guest);
        }
        for (const QString &email : *edit.guests) {
            if (std::none_of(guests.cbegin(), guests.cend(), [&email](const Guest &g) {
                    return g.email.compare(email, Qt::CaseInsensitive) == 0;
                }))
                guests.append({email, email, QStringLiteral("needsAction"), false, false});
        }
        occurrence.guests = guests;
    }
    if (edit.videoCall && !*edit.videoCall)
        occurrence.conferenceUrl.clear();

    if (edit.movesTimes()) {
        const QDateTime start = edit.start.value_or(edited.start);
        const QDateTime end = edit.end.value_or(edited.end);
        const QTimeZone view = occurrence.start.timeZone();
        QDateTime newStart = start;
        if (occurrence.eventId != edited.eventId) {
            const QTimeZone zone = start.timeZone();
            const qint64 days = edited.start.toTimeZone(zone).date().daysTo(start.date());
            newStart = QDateTime(occurrence.start.toTimeZone(zone).date().addDays(days),
                                 start.time(), zone);
        }
        occurrence.allDay = edit.allDay.value_or(edited.allDay);
        if (occurrence.allDay) {
            const qint64 length = std::max<qint64>(1, start.date().daysTo(end.date()));
            occurrence.start = QDateTime(newStart.date(), QTime(0, 0), view);
            occurrence.end = QDateTime(newStart.date().addDays(length), QTime(0, 0), view);
        } else {
            occurrence.start = newStart.toTimeZone(view);
            occurrence.end = newStart.addSecs(start.secsTo(end)).toTimeZone(view);
        }
        if (start.timeSpec() == Qt::TimeZone)
            occurrence.zone = QString::fromUtf8(start.timeZone().id());
    }
}

} // namespace callie
