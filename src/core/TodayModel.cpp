#include "callie/TodayModel.h"

#include <QLocale>

namespace callie {

TodayModel::TodayModel(QObject *parent) : QObject(parent) {}

void TodayModel::setSource(CalendarSource *source)
{
    if (m_source == source)
        return;
    if (m_source)
        disconnect(m_source, nullptr, this, nullptr);
    m_source = source;
    if (m_source)
        connect(m_source, &CalendarSource::changed, this, &TodayModel::refresh);
    refresh();
}

void TodayModel::setNow(const QDateTime &now)
{
    if (m_now == now)
        return;
    m_now = now;
    refresh();
}

void TodayModel::refresh()
{
    m_next = {};
    m_nextCalendar.clear();
    if (m_source && m_now.isValid()) {
        const QTimeZone zone = m_now.timeZone();
        const QDateTime endOfDay(m_now.date().addDays(1), QTime(0, 0), zone);
        for (const Event &event : m_source->eventsBetween(m_now, endOfDay, zone)) {
            if (event.allDay || event.start <= m_now)
                continue;
            if (!m_next.isValid() || event.start < m_next.start)
                m_next = event;
        }
        for (const CalendarInfo &calendar : m_source->calendars()) {
            if (calendar.id == m_next.calendarId)
                m_nextCalendar = calendar.displayName;
        }
    }
    Q_EMIT changed();
}

QString TodayModel::greeting() const
{
    const int hour = m_now.time().hour();
    if (hour >= 5 && hour < 12)
        return tr("Good morning");
    if (hour >= 12 && hour < 18)
        return tr("Good afternoon");
    return tr("Good evening");
}

QString TodayModel::dateLabel() const
{
    return QLocale().toString(m_now.date(), QStringLiteral("dddd, MMMM d"));
}

QString TodayModel::nextLabel() const
{
    if (!hasNext())
        return {};
    const qint64 minutes = (m_now.secsTo(m_next.start) + 59) / 60;
    if (minutes <= 60)
        return tr("Up next, in %n min", nullptr, int(minutes));
    return tr("Up next at %1").arg(m_next.start.toString(QStringLiteral("H:mm")));
}

QString TodayModel::nextDetail() const
{
    if (!hasNext())
        return {};
    const QString when = tr("%1 to %2")
                             .arg(m_next.start.toString(QStringLiteral("H:mm")),
                                  m_next.end.toString(QStringLiteral("H:mm")));
    const QString where = !m_next.location.isEmpty() ? m_next.location : m_nextCalendar;
    return where.isEmpty() ? when : tr("%1, %2").arg(when, where);
}

} // namespace callie
