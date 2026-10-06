#pragma once

#include <QDateTime>
#include <QObject>
#include <QQmlEngine>

namespace callie {

/// ViewRange for QML. Days pass as local midnights, the form a JS Date
/// round-trips without moving to another day.
class Views : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    Q_INVOKABLE QDateTime start(const QString &view, const QDateTime &focus) const;
    Q_INVOKABLE int days(const QString &view, const QDateTime &focus) const;
    Q_INVOKABLE QDateTime step(const QString &view, const QDateTime &focus, int count) const;
    Q_INVOKABLE QString heading(const QDateTime &day, const QDateTime &today) const;
};

} // namespace callie
