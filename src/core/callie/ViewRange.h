#pragma once

#include <QDate>
#include <QString>

namespace callie {

/// Which days each calendar view shows around the day it is on.
namespace ViewRange {

enum class View { Day, Week, Month, Agenda };

/// "day", "week", "month" or "agenda"; anything else is the week.
[[nodiscard]] View fromName(const QString &name);

/// How many days the agenda looks ahead.
constexpr int kAgendaDays = 30;

/// The first day shown: the day itself, its week's Monday, or the Monday on
/// or before the first of its month.
[[nodiscard]] QDate start(View view, QDate focus);

/// How many days are shown; a month is whole weeks, so 28 to 42.
[[nodiscard]] int days(View view, QDate focus);

/// The day `count` views later (or earlier, when negative). Months keep to
/// the first, so stepping from 31 January does not skip February.
[[nodiscard]] QDate step(View view, QDate focus, int count);

/// "Today", "Tomorrow" or "Yesterday" near `today`, otherwise the date
/// written out, such as "Friday, October 9".
[[nodiscard]] QString heading(QDate day, QDate today);

} // namespace ViewRange

} // namespace callie
