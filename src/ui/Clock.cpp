#include "Clock.h"

#include <QCoreApplication>

namespace callie {

Clock::Clock(QObject *parent) : QObject(parent)
{
    // Twice a minute keeps the now line and the date within a minute of the truth.
    m_tick.setInterval(30'000);
    connect(&m_tick, &QTimer::timeout, this, &Clock::nowChanged);
    m_tick.start();
}

Clock *Clock::instance()
{
    static Clock *clock = new Clock(QCoreApplication::instance());
    return clock;
}

Clock *Clock::create(QQmlEngine *, QJSEngine *)
{
    Clock *clock = instance();
    QJSEngine::setObjectOwnership(clock, QJSEngine::CppOwnership);
    return clock;
}

QDateTime Clock::now() const
{
    return m_frozen.isValid() ? m_frozen : QDateTime::currentDateTime();
}

void Clock::freeze(const QDateTime &moment)
{
    m_frozen = moment;
    m_tick.stop();
    Q_EMIT nowChanged();
}

} // namespace callie
