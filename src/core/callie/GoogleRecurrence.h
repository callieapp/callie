#pragma once

#include "Event.h"
#include "GoogleCalendarApi.h"

#include <QList>
#include <QTimeZone>

#include <optional>

namespace KCalendarCore {
class Recurrence;
}

namespace callie {

/// Fills `recurrence` from the series' RRULE, EXRULE, RDATE and EXDATE lines,
/// starting at `start`. False when no line could be used.
bool buildGoogleRecurrence(KCalendarCore::Recurrence &recurrence, const GoogleEvent &series,
                           const QDateTime &start, bool allDay);

/// The occurrences of `events`, as stored by GoogleCache for one calendar,
/// that overlap [from, to). Series are expanded in their organizer's time
/// zone, then cancelled and moved occurrences are applied. All-day events
/// start at midnight in `viewZone`.
[[nodiscard]] QList<Event> expandGoogleEvents(const QList<GoogleEvent> &events,
                                              const QDateTime &from, const QDateTime &to,
                                              const QTimeZone &viewZone);

/// When the series' last occurrence ends, in UTC, or nothing when it repeats
/// forever. An all-day series' dates are taken as UTC days.
[[nodiscard]] std::optional<QDateTime> seriesEnd(const GoogleEvent &series);

/// The days the series' EXDATE lines take out, in `zone`.
[[nodiscard]] QList<QDate> excludedDays(const GoogleEvent &series, const QTimeZone &zone);

/// A series' recurrence lines split at one of its occurrences.
struct SplitRecurrence
{
    /// The series as it was, ending the occurrence before.
    QStringList before;
    /// A new series from that occurrence on, with what is left of a count.
    QStringList after;
};

/// Splits `series` at the occurrence that originally started at `at`, a
/// midnight for an all-day series.
[[nodiscard]] SplitRecurrence splitRecurrence(const GoogleEvent &series, const QDateTime &at);

} // namespace callie
