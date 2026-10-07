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

/// The first day shown: the day itself, the start of its week, or the start
/// of the week holding the first of its month. Weeks begin on `firstDay`.
[[nodiscard]] QDate start(View view, QDate focus, Qt::DayOfWeek firstDay = Qt::Monday);

/// How many days are shown; a month is whole weeks, so 28 to 42.
[[nodiscard]] int days(View view, QDate focus, Qt::DayOfWeek firstDay = Qt::Monday);

/// The first day of the week holding `day`.
[[nodiscard]] QDate weekStart(QDate day, Qt::DayOfWeek firstDay);

/// The day `count` views later (or earlier, when negative). Months keep to
/// the first, so stepping from 31 January does not skip February.
[[nodiscard]] QDate step(View view, QDate focus, int count);

/// "Today", "Tomorrow" or "Yesterday" near `today`, otherwise the date
/// written out, such as "Friday, October 9".
[[nodiscard]] QString heading(QDate day, QDate today);

} // namespace ViewRange

} // namespace callie
