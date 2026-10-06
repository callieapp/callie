#pragma once

#include <QDateTime>
#include <QString>
#include <QTimeZone>

namespace callie {

/// An event the user is about to create, before any calendar has it.
struct EventDraft
{
    QString summary;
    QString location;
    QDateTime start;
    QDateTime end;
    bool allDay = false;
    /// CalendarInfo::id of the calendar to create it in.
    QString calendarId;

    [[nodiscard]] bool isValid() const
    {
        return !summary.isEmpty() && start.isValid() && end.isValid() && start <= end;
    }
};

/// Reads a one-line description of an event, such as "Lunch with Alex
/// tomorrow 12-1pm at Cafe Sol", with fixed English rules rather than a model.
/// What it does not recognise stays in the title.
///
/// Days: today, tomorrow, weekday names (the next one, or "next friday" for
/// the one after), "in 3 days", "oct 12", "12 october" and 2026-10-12.
/// Times: 3pm, 3:30pm, 15:00, noon, midnight; ranges like 3-4pm, 3pm to 5pm
/// and "from 3 to 4"; lengths like "for 30 min" or "for 2 hours". A day
/// without a time makes an all-day event; a time without a day means today,
/// or tomorrow once that time has passed. "at" or "@" before anything that is
/// not a time starts the location.
class QuickAdd
{
public:
    /// One hour when nothing gives a length.
    static constexpr int kDefaultMinutes = 60;

    [[nodiscard]] static EventDraft parse(const QString &text, const QDateTime &now,
                                          const QTimeZone &zone);
};

} // namespace callie
