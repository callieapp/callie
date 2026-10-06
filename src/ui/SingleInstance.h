#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>

namespace callie {

/// Keeps one Callie per session: a second launch asks the first to show its
/// window, then exits, so reminders never come twice.
class SingleInstance : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.callieapp.Callie")

public:
    SingleInstance(const QString &service, const QDBusConnection &bus, QObject *parent = nullptr);

    /// Takes the bus name. When another instance holds it, asks that one to
    /// activate and returns false. Without a session bus there is nobody to
    /// ask, so this instance runs.
    bool claim();

public Q_SLOTS:
    Q_SCRIPTABLE void Activate();

Q_SIGNALS:
    void activated();

private:
    QString m_service;
    QDBusConnection m_bus;
};

} // namespace callie
