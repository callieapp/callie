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
    return QDateTime(day.date(), QTime(0, 0), m_zone).addSecs(qint64(minutes) * 60);
}

int Times::minutesIntoDay(const QDateTime &time) const
{
    return time.toTimeZone(m_zone).time().msecsSinceStartOfDay() / 60000;
}

} // namespace callie
