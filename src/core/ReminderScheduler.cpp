#include "callie/ReminderScheduler.h"

#include <QTimeZone>

#include <algorithm>

namespace callie {

namespace {

// Rechecks at least this often, so a clock change or a resume from suspend is
// noticed without waiting for a far-off reminder.
constexpr int kLongestWaitMs = 5 * 60 * 1000;

// A reminder that fell due shortly before Callie started is still said.
constexpr qint64 kStartupGraceSecs = 5 * 60;

QString keyOf(const Event &event, int minutes)
{
    return event.calendarId + u'|' + event.uid + u'|' + event.start.toUTC().toString(Qt::ISODate) +
           u'|' + QString::number(minutes);
}

} // namespace

ReminderScheduler::ReminderScheduler(QObject *parent)
    : QObject(parent), m_now([] { return QDateTime::currentDateTimeUtc(); })
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &ReminderScheduler::check);
}

void ReminderScheduler::setSource(CalendarSource *source)
{
    if (m_source == source)
        return;
    if (m_source)
        disconnect(m_source, nullptr, this, nullptr);
    m_source = source;
    if (m_source)
        connect(m_source, &CalendarSource::changed, this, &ReminderScheduler::check);
    check();
}

void ReminderScheduler::setNow(Now now)
{
    m_now = std::move(now);
}

void ReminderScheduler::setDefaultMinutes(int minutes)
{
    m_defaultMinutes = minutes;
    check();
}

void ReminderScheduler::setHiddenCalendars(const QStringList &calendars)
{
    m_hidden = calendars;
}

void ReminderScheduler::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    m_lastCheck = {};
    if (m_enabled) {
        check();
    } else {
        m_timer.stop();
        m_snoozed.clear();
    }
}

void ReminderScheduler::snooze(const Event &event, int minutes)
{
    m_snoozed.append({event, m_now().addSecs(qint64(minutes) * 60)});
    check();
}

QList<int> ReminderScheduler::remindersFor(const Event &event) const
{
    if (event.remindersKnown)
        return event.reminders;
    // A default before midnight would be no use for an all-day event.
    if (event.allDay || m_defaultMinutes < 0)
        return {};
    return {m_defaultMinutes};
}

void ReminderScheduler::check()
{
    if (!m_enabled)
        return;
    const QDateTime now = m_now();
    const QDateTime since = m_lastCheck.isValid() ? m_lastCheck : now.addSecs(-kStartupGraceSecs);
    m_lastCheck = now;
    QDateTime next;
    const auto consider = [&next](const QDateTime &at) {
        if (!next.isValid() || at < next)
            next = at;
    };

    for (auto it = m_snoozed.begin(); it != m_snoozed.end();) {
        if (it->at <= now) {
            const Event event = it->event;
            it = m_snoozed.erase(it);
            Q_EMIT due(event, -1);
        } else {
            consider(it->at);
            ++it;
        }
    }

    if (m_source) {
        // Events that already ended are left out: a reminder for one is no use,
        // even if it fell due while the computer slept.
        const QList<Event> events =
            m_source->eventsBetween(now, now.addDays(kLookAheadDays), QTimeZone::UTC);
        for (const Event &event : events) {
            if (event.declined || m_hidden.contains(event.calendarId))
                continue;
            for (int minutes : remindersFor(event)) {
                const QDateTime at = event.start.addSecs(-qint64(minutes) * 60);
                if (at > now) {
                    consider(at);
                    continue;
                }
                const QString key = keyOf(event, minutes);
                if (at <= since || m_said.contains(key))
                    continue;
                m_said.insert(key, at);
                Q_EMIT due(event, minutes);
            }
        }
    }

    // Keys are only needed while their reminder could still be seen again.
    const QDateTime stale = now.addDays(-kLookAheadDays);
    m_said.removeIf([&stale](const auto &said) { return said.value() < stale; });
    schedule(now, next);
}

void ReminderScheduler::schedule(const QDateTime &now, const QDateTime &next)
{
    qint64 wait = kLongestWaitMs;
    if (next.isValid())
        wait = std::clamp<qint64>(now.msecsTo(next), 0, kLongestWaitMs);
    m_timer.start(int(wait));
}

} // namespace callie
