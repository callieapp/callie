#pragma once

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickWindow>

namespace callie {

/// Follows the mouse over a whole window while active, whatever item is under
/// it or holds it, so a drag can outlive the item it started on.
class PointerWatch : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickWindow *window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

public:
    using QObject::QObject;

    [[nodiscard]] QQuickWindow *window() const { return m_window; }
    void setWindow(QQuickWindow *window);
    [[nodiscard]] bool active() const { return m_active; }
    void setActive(bool active);

Q_SIGNALS:
    void windowChanged();
    void activeChanged();
    /// The mouse moved to `scenePosition`, in the window's scene.
    void moved(QPointF scenePosition);
    /// The button was let go at `scenePosition`; the watch stops.
    void released(QPointF scenePosition);
    /// Escape was pressed; the watch stops.
    void cancelled();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void watch();

    QPointer<QQuickWindow> m_window;
    bool m_active = false;
};

} // namespace callie
