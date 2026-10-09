#pragma once

#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimeZone>

#include <optional>

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

/// A repeat the custom form can say: every `interval` days, weeks, months or
/// years, on some weekdays or on a day of the month, until a date or for a
/// number of times.
struct Custom
{
    /// "daily", "weekly", "monthly" or "yearly".
    QString frequency = QStringLiteral("weekly");
    int interval = 1;
    /// For weekly: Qt day numbers, Monday 1 to Sunday 7.
    QList<int> weekdays;
    /// For monthly: on, say, the second Tuesday rather than on the 14th.
    bool onWeekday = false;
    /// The last day it can happen on, or invalid.
    QDate until;
    /// How many times it happens, or 0.
    int count = 0;
    /// The rule's WKST, which decides the weeks of a rule every few weeks on
    /// several days; empty for the standard Monday.
    QString weekStart = {};

    friend bool operator==(const Custom &, const Custom &) = default;
};

/// `recurrence` as the custom form says it, read from `start`; empty when the
/// form cannot say it, such as a rule on several days of the month.
[[nodiscard]] std::optional<Custom> custom(const QStringList &recurrence, QDate start,
                                           const QTimeZone &zone);
/// The rule for `custom` from `start`. An until date is written as the end of
/// that day in `zone`, or as a date for an all-day event.
[[nodiscard]] QStringList rule(const Custom &custom, QDate start, bool allDay,
                               const QTimeZone &zone);
/// `custom` for a start moved `days` on: a weekly rule's weekdays move with it.
[[nodiscard]] Custom shifted(Custom custom, int days);
/// `custom` in words, such as "Every 2 weeks on Monday and Wednesday, 5 times".
[[nodiscard]] QString describe(const Custom &custom, QDate start);

} // namespace Repeat

} // namespace callie
