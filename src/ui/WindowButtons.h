#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QStringList>

namespace callie {

/// Which window buttons go on which side of the title bar, following the
/// desktop's own setting. Each side lists "minimize", "maximize" and "close" in
/// order; a button the desktop leaves out is left out here too.
class WindowButtons : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(WindowButtons)
    QML_SINGLETON
    Q_PROPERTY(QStringList left READ left CONSTANT)
    Q_PROPERTY(QStringList right READ right CONSTANT)

public:
    struct Layout
    {
        QStringList left;
        QStringList right;
    };

    static WindowButtons *create(QQmlEngine *, QJSEngine *);

    /// GNOME's org.gnome.desktop.wm.preferences button-layout, such as
    /// "appmenu:minimize,maximize,close".
    [[nodiscard]] static Layout fromGnome(const QString &setting);
    /// KDE's ButtonsOnLeft and ButtonsOnRight from kwinrc, such as "MS" and "IAX".
    [[nodiscard]] static Layout fromKde(const QString &left, const QString &right);
    /// The running desktop's layout, or minimize, maximize and close on the right.
    [[nodiscard]] static Layout detect();

    [[nodiscard]] QStringList left() const { return m_layout.left; }
    [[nodiscard]] QStringList right() const { return m_layout.right; }

private:
    explicit WindowButtons(Layout layout, QObject *parent = nullptr);

    Layout m_layout;
};

} // namespace callie
