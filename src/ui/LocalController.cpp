#include "LocalController.h"

#include "callie/LocalCalendars.h"

#include <QCoreApplication>

using namespace Qt::StringLiterals;

namespace callie {

LocalController::LocalController(QObject *parent) : QObject(parent) {}

LocalController *LocalController::instance()
{
    static auto *local = new LocalController(QCoreApplication::instance());
    return local;
}

LocalController *LocalController::create(QQmlEngine *, QJSEngine *)
{
    LocalController *local = instance();
    QJSEngine::setObjectOwnership(local, QJSEngine::CppOwnership);
    return local;
}

void LocalController::setUp(LocalCalendars *calendars)
{
    if (m_calendars)
        disconnect(m_calendars, nullptr, this, nullptr);
    m_calendars = calendars;
    if (m_calendars)
        connect(m_calendars, &CalendarSource::changed, this, &LocalController::calendarsChanged);
    Q_EMIT calendarsChanged();
}

QVariantList LocalController::calendars() const
{
    QVariantList list;
    if (!m_calendars)
        return list;
    for (const CalendarInfo &calendar : m_calendars->calendars())
        list << QVariantMap{{u"id"_s, calendar.id},
                            {u"name"_s, calendar.displayName},
                            {u"color"_s, calendar.color}};
    return list;
}

QString LocalController::add(const QString &name)
{
    if (!m_calendars) {
        setError(tr("Calendars on this computer cannot be changed here."));
        return {};
    }
    QString error;
    const QString id = m_calendars->create(name, {}, &error);
    setError(error);
    return id;
}

bool LocalController::remove(const QString &id)
{
    QString error;
    const bool removed = m_calendars && m_calendars->remove(id, &error);
    setError(error);
    return removed;
}

void LocalController::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    Q_EMIT errorChanged();
}

} // namespace callie
