#include "NotificationServer.h"

#include "callie/Logging.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QVariantMap>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

const QString kService = u"org.freedesktop.Notifications"_s;
const QString kPath = u"/org/freedesktop/Notifications"_s;
const QString kInterface = u"org.freedesktop.Notifications"_s;

} // namespace

FreedesktopNotifications::FreedesktopNotifications(QObject *parent) : NotificationServer(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(kService, kPath, kInterface, u"ActionInvoked"_s, this,
                SLOT(onActionInvoked(uint, QString)));
    bus.connect(kService, kPath, kInterface, u"NotificationClosed"_s, this,
                SLOT(onClosed(uint, uint)));
}

void FreedesktopNotifications::show(const QString &title, const QString &body,
                                    const QStringList &actions, std::function<void(uint)> shown)
{
    QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath, kInterface, u"Notify"_s);
    const QVariantMap hints{
        {u"desktop-entry"_s, u"org.callieapp.Callie"_s},
        {u"category"_s, u"x-gnome.calendar"_s},
        // Normal urgency; the daemon decides how long it stays.
        {u"urgency"_s, QVariant::fromValue(uchar(1))},
    };
    call << u"Callie"_s << uint(0) << u"org.callieapp.Callie"_s << title << body << actions << hints
         << int(-1);
    auto *watcher =
        new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [shown = std::move(shown)](QDBusPendingCallWatcher *watcher) {
                const QDBusPendingReply<uint> reply = *watcher;
                watcher->deleteLater();
                if (reply.isError())
                    qCWarning(lcUi) << "could not show a notification:" << reply.error().message();
                shown(reply.isError() ? 0 : reply.value());
            });
}

void FreedesktopNotifications::close(uint id)
{
    QDBusConnection::sessionBus().asyncCall(
        QDBusMessage::createMethodCall(kService, kPath, kInterface, u"CloseNotification"_s) << id);
}

void FreedesktopNotifications::onActionInvoked(uint id, const QString &key)
{
    Q_EMIT actionInvoked(id, key);
}

void FreedesktopNotifications::onClosed(uint id, uint reason)
{
    Q_UNUSED(reason)
    Q_EMIT closed(id);
}

} // namespace callie
