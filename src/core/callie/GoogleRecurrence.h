#pragma once

#include "Event.h"
#include "GoogleCalendarApi.h"

#include <QList>
#include <QTimeZone>

namespace callie {

/// The occurrences of `events`, as stored by GoogleCache for one calendar,
/// that overlap [from, to). Series are expanded in their organizer's time
/// zone, then cancelled and moved occurrences are applied. All-day events
/// start at midnight in `viewZone`.
[[nodiscard]] QList<Event> expandGoogleEvents(const QList<GoogleEvent> &events,
                                              const QDateTime &from, const QDateTime &to,
                                              const QTimeZone &viewZone);

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
