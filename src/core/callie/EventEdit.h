#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <optional>

namespace callie {

struct Event;

/// Which occurrences of a repeating event a change is for.
enum class EditScope {
    /// Only the occurrence; for a one-off event, the event.
    ThisEvent,
    /// This occurrence and every one after it, which splits the series.
    ThisAndFollowing,
    /// Every occurrence, past ones included.
    AllEvents,
};

/// Changes to make to an event. Only what is set changes; the rest stays.
struct EventEdit
{
    std::optional<QString> summary;
    std::optional<QString> location;
    std::optional<QString> description;
    /// New times, in the zone the event is to be written in. For a whole series,
    /// the shift from the occurrence's times moves every occurrence.
    std::optional<QDateTime> start;
    std::optional<QDateTime> end;
    std::optional<bool> allDay;
    /// The repeat rule as RFC 5545 lines, such as "RRULE:FREQ=WEEKLY;BYDAY=MO";
    /// an empty list stops the event repeating.
    std::optional<QStringList> recurrence;
    /// Every guest's address but the user's; replaces the list, keeping the
    /// answers of guests who stay.
    std::optional<QStringList> guests;
    /// True adds a video call to the event, false removes it.
    std::optional<bool> videoCall;

    [[nodiscard]] bool isEmpty() const
    {
        return !summary && !location && !description && !start && !end && !allDay && !recurrence &&
               !guests && !videoCall;
    }
    /// Whether the times change, which moves the event.
    [[nodiscard]] bool movesTimes() const { return start || end || allDay; }
};

/// Puts `edit`, made on `edited`, on `occurrence`: `edited` itself, or another
/// occurrence of its series when the edit is for all of them. Another
/// occurrence moves by as many days as `edited` did, to its new time of day.
void applyEdit(Event &occurrence, const Event &edited, const EventEdit &edit);

} // namespace callie
