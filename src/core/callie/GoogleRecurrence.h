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

} // namespace callie
