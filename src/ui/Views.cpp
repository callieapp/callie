#include "Views.h"

#include "callie/ViewRange.h"

namespace callie {

QDateTime Views::start(const QString &view, const QDateTime &focus, int firstDay) const
{
    return ViewRange::start(ViewRange::fromName(view), focus.date(), Qt::DayOfWeek(firstDay))
        .startOfDay();
}

int Views::days(const QString &view, const QDateTime &focus, int firstDay) const
{
    return ViewRange::days(ViewRange::fromName(view), focus.date(), Qt::DayOfWeek(firstDay));
}

int Views::weekNumber(const QDateTime &day, int firstDay) const
{
    return ViewRange::weekNumber(day.date(), Qt::DayOfWeek(firstDay));
}

QDateTime Views::step(const QString &view, const QDateTime &focus, int count) const
{
    return ViewRange::step(ViewRange::fromName(view), focus.date(), count).startOfDay();
}

QDateTime Views::dayAt(const QDateTime &start, int index) const
{
    return start.date().addDays(index).startOfDay();
}

int Views::daysBetween(const QDateTime &from, const QDateTime &to) const
{
    return int(from.date().daysTo(to.date()));
}

QString Views::heading(const QDateTime &day, const QDateTime &today) const
{
    return ViewRange::heading(day.date(), today.date());
}

} // namespace callie
