#include "ServerCalendarsController.h"

#include "callie/CalDavSource.h"

#include <QCoreApplication>

using namespace Qt::StringLiterals;

namespace callie {

ServerCalendarsController::ServerCalendarsController(QObject *parent) : QObject(parent) {}

ServerCalendarsController *ServerCalendarsController::instance()
{
    static auto *controller = new ServerCalendarsController(QCoreApplication::instance());
    return controller;
}

ServerCalendarsController *ServerCalendarsController::create(QQmlEngine *, QJSEngine *)
{
    ServerCalendarsController *controller = instance();
    QJSEngine::setObjectOwnership(controller, QJSEngine::CppOwnership);
    return controller;
}

void ServerCalendarsController::setUp(CalDavSource *source)
{
    if (m_source)
        disconnect(m_source, nullptr, this, nullptr);
    m_source = source;
    if (m_source)
        connect(m_source, &CalendarSource::changed, this,
                &ServerCalendarsController::calendarsChanged);
    Q_EMIT calendarsChanged();
}

QVariantMap ServerCalendarsController::calendars() const
{
    QVariantMap byAccount;
    for (const CalendarInfo &calendar : m_source ? m_source->calendars() : QList<CalendarInfo>()) {
        QVariantList list = byAccount.value(calendar.account).toList();
        list << QVariantMap{{u"id"_s, calendar.id},
                            {u"name"_s, calendar.displayName},
                            {u"color"_s, calendar.color}};
        byAccount.insert(calendar.account, list);
    }
    return byAccount;
}

void ServerCalendarsController::add(const QString &accountId, const QString &name)
{
    if (!m_source || m_busy)
        return;
    m_busy = true;
    m_error.clear();
    Q_EMIT stateChanged();
    const QPointer<ServerCalendarsController> self(this);
    m_source->createCalendar(accountId, name, {},
                             [self, accountId](const QString &, const QString &error) {
                                 if (!self)
                                     return;
                                 self->finish(accountId, error);
                                 if (error.isEmpty())
                                     Q_EMIT self->made(accountId);
                             });
}

void ServerCalendarsController::remove(const QString &calendarId)
{
    if (!m_source || m_busy)
        return;
    QString accountId;
    for (const CalendarInfo &calendar : m_source->calendars()) {
        if (calendar.id == calendarId)
            accountId = calendar.account;
    }
    m_busy = true;
    m_error.clear();
    Q_EMIT stateChanged();
    const QPointer<ServerCalendarsController> self(this);
    m_source->removeCalendar(calendarId, [self, accountId](const QString &error) {
        if (self)
            self->finish(accountId, error);
    });
}

void ServerCalendarsController::finish(const QString &accountId, const QString &error)
{
    m_busy = false;
    m_error = error;
    m_errorAccount = error.isEmpty() ? QString() : accountId;
    Q_EMIT stateChanged();
}

} // namespace callie
