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

    /// `firstDay` is the week's first day, 1 (Monday) to 7 (Sunday).
    Q_INVOKABLE QDateTime start(const QString &view, const QDateTime &focus, int firstDay) const;
    Q_INVOKABLE int days(const QString &view, const QDateTime &focus, int firstDay) const;
    /// The ISO number of the week holding `day`, for weeks starting on `firstDay`.
    Q_INVOKABLE int weekNumber(const QDateTime &day, int firstDay) const;
    Q_INVOKABLE QDateTime step(const QString &view, const QDateTime &focus, int count) const;
    /// The day `index` days after `start`.
    Q_INVOKABLE QDateTime dayAt(const QDateTime &start, int index) const;
    Q_INVOKABLE QString heading(const QDateTime &day, const QDateTime &today) const;
};

} // namespace callie
