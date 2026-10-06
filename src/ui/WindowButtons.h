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
    Q_PROPERTY(QStringList left READ left NOTIFY changed)
    Q_PROPERTY(QStringList right READ right NOTIFY changed)

public:
    struct Layout
    {
        QStringList left;
        QStringList right;
    };

    static WindowButtons *create(QQmlEngine *, QJSEngine *);

    /// Starts with minimize, maximize and close on the right, then follows the
    /// desktop: kwinrc is read at once, GNOME's setting arrives without blocking.
    explicit WindowButtons(QObject *parent = nullptr);

    /// GNOME's org.gnome.desktop.wm.preferences button-layout, such as
    /// "appmenu:minimize,maximize,close".
    [[nodiscard]] static Layout fromGnome(const QString &setting);
    /// KDE's ButtonsOnLeft and ButtonsOnRight from kwinrc, such as "MS" and "IAX".
    [[nodiscard]] static Layout fromKde(const QString &left, const QString &right);
    [[nodiscard]] QStringList left() const { return m_layout.left; }
    [[nodiscard]] QStringList right() const { return m_layout.right; }

Q_SIGNALS:
    void changed();

private:
    void readGnome();
    void apply(const Layout &layout);

    Layout m_layout;
};

} // namespace callie
