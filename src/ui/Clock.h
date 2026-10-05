#pragma once

#include <QDateTime>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>

namespace callie {

/// The one source of "now" for QML, so it can be frozen for screenshots and
/// tests instead of each view calling new Date().
class Clock : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Clock)
    QML_SINGLETON
    Q_PROPERTY(QDateTime now READ now NOTIFY nowChanged)

public:
    static Clock *instance();
    static Clock *create(QQmlEngine *, QJSEngine *);

    [[nodiscard]] QDateTime now() const;

    /// Stops the clock at `moment`, as `--now` asks.
    void freeze(const QDateTime &moment);

Q_SIGNALS:
    void nowChanged();

private:
    explicit Clock(QObject *parent);

    QTimer m_tick;
    QDateTime m_frozen;
};

} // namespace callie
