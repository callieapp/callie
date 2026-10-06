#include "callie/Times.h"

#include "callie/TimeFormat.h"

namespace callie {

Times::Times(const QTimeZone &zone, bool use24Hour, QObject *parent)
    : QObject(parent), m_zone(zone), m_use24Hour(use24Hour)
{}

QString Times::time(const QDateTime &time) const
{
    return formatClock(time.toTimeZone(m_zone).time(), m_use24Hour);
}

QString Times::hour(int hour) const
{
    return formatHourLabel(hour, m_use24Hour);
}

QDateTime Times::date(const QDateTime &time) const
{
    return time.toTimeZone(m_zone).date().startOfDay();
}

QDateTime Times::at(const QDateTime &day, int minutes) const
{
    // Wall-clock, like the grid: on a DST day 13:00 is still 13:00, not 13 hours in.
    const QDate date = day.date().addDays(minutes / (24 * 60));
    const int rest = minutes % (24 * 60);
    return QDateTime(date, QTime(rest / 60, rest % 60), m_zone);
}

int Times::minutesIntoDay(const QDateTime &time) const
{
    return time.toTimeZone(m_zone).time().msecsSinceStartOfDay() / 60000;
}

} // namespace callie
