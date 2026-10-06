#pragma once

#include <QString>
#include <QTime>

namespace callie {

/// A clock time as the user wants it: "14:30" or "2:30 PM".
[[nodiscard]] QString formatClock(QTime time, bool use24Hour);

/// An hour label for the grid: "14:00" or "2 PM".
[[nodiscard]] QString formatHourLabel(int hour, bool use24Hour);

} // namespace callie
