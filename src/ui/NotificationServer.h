#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace callie {

/// Somewhere to show desktop notifications.
class NotificationServer : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    /// Shows a notification; `actions` alternates keys and their labels, and
    /// the key "default" is a click on the notification itself. `shown` gets
    /// the notification's id, or 0 if it could not be shown.
    virtual void show(const QString &title, const QString &body, const QStringList &actions,
                      std::function<void(uint id)> shown) = 0;
    virtual void close(uint id) = 0;

Q_SIGNALS:
    void actionInvoked(uint id, const QString &key);
    void closed(uint id);
};

/// The desktop's notification daemon, over org.freedesktop.Notifications.
class FreedesktopNotifications : public NotificationServer
{
    Q_OBJECT

public:
    explicit FreedesktopNotifications(QObject *parent = nullptr);

    void show(const QString &title, const QString &body, const QStringList &actions,
              std::function<void(uint id)> shown) override;
    void close(uint id) override;

private Q_SLOTS:
    void onActionInvoked(uint id, const QString &key);
    void onClosed(uint id, uint reason);
};

} // namespace callie
