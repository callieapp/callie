#pragma once

#include <QDate>
#include <QString>
#include <QStringList>

namespace callie {

/// The repeat choices the event editor offers, as RFC 5545 rules. Each is
/// read from the day the event starts on, so "weekly" means on that weekday.
namespace Repeat {

/// "none", "daily", "weekdays", "weekly", "monthly" (on that day of the month),
/// "monthlyWeekday" (on, say, the second Tuesday) or "yearly".
[[nodiscard]] QStringList choices();

/// The rule for `choice` from `start`; empty for "none" or an unknown choice.
[[nodiscard]] QStringList rule(const QString &choice, QDate start);

/// Which choice `recurrence` is, read from `start`: "none" when empty, and
/// "custom" when it is none of the choices, such as every two weeks.
[[nodiscard]] QString choiceOf(const QStringList &recurrence, QDate start);

/// A choice in words for `start`, such as "Weekly on Tuesday" or "Monthly on
/// the second Tuesday".
[[nodiscard]] QString describe(const QString &choice, QDate start);

} // namespace Repeat

} // namespace callie
