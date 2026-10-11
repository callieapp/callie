#include "PointerWatch.h"

#include <QKeyEvent>
#include <QMouseEvent>

namespace callie {

void PointerWatch::setWindow(QQuickWindow *window)
{
    if (window == m_window)
        return;
    if (m_window)
        m_window->removeEventFilter(this);
    m_window = window;
    watch();
    Q_EMIT windowChanged();
}

void PointerWatch::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    watch();
    Q_EMIT activeChanged();
}

void PointerWatch::watch()
{
    if (!m_window)
        return;
    m_window->removeEventFilter(this);
    if (m_active)
        m_window->installEventFilter(this);
}

bool PointerWatch::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseMove: Q_EMIT moved(static_cast<QMouseEvent *>(event)->scenePosition()); break;
    case QEvent::MouseButtonRelease: {
        const QPointF at = static_cast<QMouseEvent *>(event)->scenePosition();
        setActive(false);
        Q_EMIT released(at);
        break;
    }
    case QEvent::KeyPress:
        if (static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            setActive(false);
            Q_EMIT cancelled();
            return true;
        }
        break;
    default: break;
    }
    return QObject::eventFilter(watched, event);
}

} // namespace callie
