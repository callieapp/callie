#pragma once

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>

class QDBusServiceWatcher;

namespace callie {

/// One entry of a com.canonical.dbusmenu layout: (ia{sv}av), its children
/// being entries again, each in a variant.
struct DBusMenuEntry
{
    int id = 0;
    QVariantMap properties;
    QVariantList children;
};

/// An entry's properties on their own: (ia{sv}).
struct DBusMenuProperties
{
    int id = 0;
    QVariantMap properties;
};

/// An ARGB32 image in network byte order, as tray hosts take icons: (iiay).
struct DBusTrayImage
{
    int width = 0;
    int height = 0;
    QByteArray pixels;
};

/// A tray tooltip: icon name, images, title and text, (sa(iiay)ss).
struct DBusTrayToolTip
{
    QString iconName;
    QList<DBusTrayImage> images;
    QString title;
    QString text;
};

QDBusArgument &operator<<(QDBusArgument &argument, const DBusMenuEntry &entry);
const QDBusArgument &operator>>(const QDBusArgument &argument, DBusMenuEntry &entry);
QDBusArgument &operator<<(QDBusArgument &argument, const DBusMenuProperties &properties);
const QDBusArgument &operator>>(const QDBusArgument &argument, DBusMenuProperties &properties);
QDBusArgument &operator<<(QDBusArgument &argument, const DBusTrayImage &image);
const QDBusArgument &operator>>(const QDBusArgument &argument, DBusTrayImage &image);
QDBusArgument &operator<<(QDBusArgument &argument, const DBusTrayToolTip &toolTip);
const QDBusArgument &operator>>(const QDBusArgument &argument, DBusTrayToolTip &toolTip);

class TrayStatusItem;
class TrayMenu;

/// Callie's entry in the desktop's system tray, over the StatusNotifierItem
/// and dbusmenu D-Bus interfaces that KDE Plasma and most other panels read.
/// A click opens the window; its menu opens it, syncs, or quits.
class TrayItem : public QObject
{
    Q_OBJECT

public:
    /// `watcher` is the bus name of the tray host's registry; tests pass their own.
    explicit TrayItem(const QDBusConnection &bus,
                      const QString &watcher = QStringLiteral("org.kde.StatusNotifierWatcher"),
                      QObject *parent = nullptr);
    ~TrayItem() override;

    /// Shows or takes away the entry.
    void setVisible(bool visible);
    [[nodiscard]] bool isVisible() const { return m_visible; }
    /// The bus name the entry is served under while it shows.
    [[nodiscard]] QString serviceName() const { return m_service; }

Q_SIGNALS:
    void openRequested();
    void syncRequested();
    void quitRequested();

private:
    void announce();

    QDBusConnection m_bus;
    QString m_watcher;
    QString m_service;
    bool m_visible = false;
    TrayStatusItem *m_item;
    TrayMenu *m_menu;
    QDBusServiceWatcher *m_watching;
};

/// org.kde.StatusNotifierItem, served at /StatusNotifierItem.
class TrayStatusItem : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierItem")
    Q_PROPERTY(QString Category READ category)
    Q_PROPERTY(QString Id READ id)
    Q_PROPERTY(QString Title READ title)
    Q_PROPERTY(QString Status READ status)
    Q_PROPERTY(int WindowId READ windowId)
    Q_PROPERTY(QString IconName READ iconName)
    Q_PROPERTY(QList<callie::DBusTrayImage> IconPixmap READ iconPixmap)
    Q_PROPERTY(QString OverlayIconName READ none)
    Q_PROPERTY(QList<callie::DBusTrayImage> OverlayIconPixmap READ noImages)
    Q_PROPERTY(QString AttentionIconName READ none)
    Q_PROPERTY(QList<callie::DBusTrayImage> AttentionIconPixmap READ noImages)
    Q_PROPERTY(QString AttentionMovieName READ none)
    Q_PROPERTY(callie::DBusTrayToolTip ToolTip READ toolTip)
    Q_PROPERTY(bool ItemIsMenu READ itemIsMenu)
    Q_PROPERTY(QDBusObjectPath Menu READ menu)

public:
    explicit TrayStatusItem(QObject *parent = nullptr);

    [[nodiscard]] QString category() const { return QStringLiteral("ApplicationStatus"); }
    [[nodiscard]] QString id() const { return QStringLiteral("callie"); }
    [[nodiscard]] QString title() const { return QStringLiteral("Callie"); }
    [[nodiscard]] QString status() const { return QStringLiteral("Active"); }
    [[nodiscard]] int windowId() const { return 0; }
    [[nodiscard]] QString iconName() const { return m_iconName; }
    [[nodiscard]] QList<DBusTrayImage> iconPixmap() const { return m_iconPixmap; }
    [[nodiscard]] QString none() const { return {}; }
    [[nodiscard]] QList<DBusTrayImage> noImages() const { return {}; }
    [[nodiscard]] DBusTrayToolTip toolTip() const;
    [[nodiscard]] bool itemIsMenu() const { return false; }
    [[nodiscard]] QDBusObjectPath menu() const;

public Q_SLOTS:
    Q_SCRIPTABLE void Activate(int x, int y);
    Q_SCRIPTABLE void SecondaryActivate(int x, int y);
    Q_SCRIPTABLE void ContextMenu(int x, int y);
    Q_SCRIPTABLE void Scroll(int delta, const QString &orientation);

Q_SIGNALS:
    void activated();

private:
    QString m_iconName;
    QList<DBusTrayImage> m_iconPixmap;
};

/// com.canonical.dbusmenu, served at /MenuBar: the entry's menu.
class TrayMenu : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.canonical.dbusmenu")
    Q_PROPERTY(uint Version READ version)
    Q_PROPERTY(QString TextDirection READ textDirection)
    Q_PROPERTY(QString Status READ status)
    Q_PROPERTY(QStringList IconThemePath READ iconThemePath)

public:
    /// The menu's entries; 0 is its root.
    enum Entry { Open = 1, Sync, Separator, Quit };

    explicit TrayMenu(QObject *parent = nullptr) : QObject(parent) {}

    [[nodiscard]] uint version() const { return 3; }
    [[nodiscard]] QString textDirection() const { return QStringLiteral("ltr"); }
    [[nodiscard]] QString status() const { return QStringLiteral("normal"); }
    [[nodiscard]] QStringList iconThemePath() const { return {}; }

public Q_SLOTS:
    Q_SCRIPTABLE uint GetLayout(int parentId, int recursionDepth, const QStringList &propertyNames,
                                callie::DBusMenuEntry &layout);
    Q_SCRIPTABLE QList<callie::DBusMenuProperties> GetGroupProperties(const QList<int> &ids,
                                                                      const QStringList &names);
    Q_SCRIPTABLE QDBusVariant GetProperty(int id, const QString &name);
    Q_SCRIPTABLE void Event(int id, const QString &eventId, const QDBusVariant &data,
                            uint timestamp);
    Q_SCRIPTABLE bool AboutToShow(int id);

Q_SIGNALS:
    Q_SCRIPTABLE void LayoutUpdated(uint revision, int parent);
    void picked(int entry);

private:
    [[nodiscard]] QVariantMap propertiesOf(int id) const;
};

} // namespace callie

Q_DECLARE_METATYPE(callie::DBusMenuEntry)
Q_DECLARE_METATYPE(callie::DBusMenuProperties)
Q_DECLARE_METATYPE(callie::DBusTrayImage)
Q_DECLARE_METATYPE(callie::DBusTrayToolTip)
