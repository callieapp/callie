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
    const quint64 generation = ++m_generation;
    if (!m_source || !m_now.isValid()) {
        apply({});
        return;
    }
    const QTimeZone zone = m_now.timeZone();
    const QDateTime endOfDay(m_now.date().addDays(1), QTime(0, 0), zone);
    QFuture<SourceSnapshot> future = m_source->load(m_now, endOfDay, zone);
    if (future.isFinished()) {
        apply(future.result());
        return;
    }
    // The greeting and date follow the clock at once; the next event follows
    // when the read lands, and one that has started meanwhile is dropped.
    if (m_next.isValid() && m_next.start <= m_now) {
        m_next = {};
        m_nextCalendar.clear();
    }
    Q_EMIT changed();
    future.then(this, [this, generation](const SourceSnapshot &snapshot) {
        if (generation == m_generation)
            apply(snapshot);
    });
}

void TodayModel::apply(const SourceSnapshot &snapshot)
{
    m_next = {};
    m_nextCalendar.clear();
    for (const Event &event : snapshot.events) {
        // A declined event is not on the user's way.
        if (event.allDay || event.declined || event.start <= m_now)
            continue;
        if (!m_next.isValid() || event.start < m_next.start)
            m_next = event;
    }
    for (const CalendarInfo &calendar : snapshot.calendars) {
        if (calendar.id == m_next.calendarId)
            m_nextCalendar = calendar.displayName;
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
