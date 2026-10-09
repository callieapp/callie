#include "TrayItem.h"

#include "callie/Logging.h"

#include <QCoreApplication>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusServiceWatcher>
#include <QIcon>
#include <QImage>
#include <QtEndian>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

const QString kItemPath = u"/StatusNotifierItem"_s;
const QString kMenuPath = u"/MenuBar"_s;
const QString kAppId = u"org.callieapp.Callie"_s;

void registerTypes()
{
    static const bool done = [] {
        qDBusRegisterMetaType<DBusMenuEntry>();
        qDBusRegisterMetaType<QList<DBusMenuProperties>>();
        qDBusRegisterMetaType<DBusMenuProperties>();
        qDBusRegisterMetaType<DBusTrayImage>();
        qDBusRegisterMetaType<QList<DBusTrayImage>>();
        qDBusRegisterMetaType<DBusTrayToolTip>();
        return true;
    }();
    Q_UNUSED(done);
}

// The bundled logo at the sizes panels ask for, for hosts without Callie's
// icon in their theme, as when it runs from a build.
QList<DBusTrayImage> logoImages()
{
    const QImage logo(u":/callie/assets/logo.png"_s);
    QList<DBusTrayImage> images;
    if (logo.isNull())
        return images;
    for (int size : {22, 32, 48}) {
        const QImage image = logo.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                                 .convertToFormat(QImage::Format_ARGB32);
        DBusTrayImage tray{image.width(), image.height(), {}};
        tray.pixels.resize(qsizetype(image.width()) * image.height() * 4);
        auto *out = reinterpret_cast<quint32 *>(tray.pixels.data());
        for (int y = 0; y < image.height(); ++y) {
            const auto *line = reinterpret_cast<const quint32 *>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x)
                *out++ = qToBigEndian(line[x]);
        }
        images.append(tray);
    }
    return images;
}

} // namespace

QDBusArgument &operator<<(QDBusArgument &argument, const DBusMenuEntry &entry)
{
    argument.beginStructure();
    argument << entry.id << entry.properties;
    argument.beginArray(QMetaType::fromType<QDBusVariant>());
    for (const QVariant &child : entry.children)
        argument << QDBusVariant(child);
    argument.endArray();
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, DBusMenuEntry &entry)
{
    argument.beginStructure();
    argument >> entry.id >> entry.properties;
    entry.children.clear();
    argument.beginArray();
    while (!argument.atEnd()) {
        QDBusVariant child;
        argument >> child;
        entry.children.append(child.variant());
    }
    argument.endArray();
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const DBusMenuProperties &properties)
{
    argument.beginStructure();
    argument << properties.id << properties.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, DBusMenuProperties &properties)
{
    argument.beginStructure();
    argument >> properties.id >> properties.properties;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const DBusTrayImage &image)
{
    argument.beginStructure();
    argument << image.width << image.height << image.pixels;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, DBusTrayImage &image)
{
    argument.beginStructure();
    argument >> image.width >> image.height >> image.pixels;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const DBusTrayToolTip &toolTip)
{
    argument.beginStructure();
    argument << toolTip.iconName << toolTip.images << toolTip.title << toolTip.text;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, DBusTrayToolTip &toolTip)
{
    argument.beginStructure();
    argument >> toolTip.iconName >> toolTip.images >> toolTip.title >> toolTip.text;
    argument.endStructure();
    return argument;
}

TrayStatusItem::TrayStatusItem(QObject *parent) : QObject(parent)
{
    if (QIcon::hasThemeIcon(kAppId))
        m_iconName = kAppId;
    else
        m_iconPixmap = logoImages();
}

DBusTrayToolTip TrayStatusItem::toolTip() const
{
    return {m_iconName, m_iconPixmap, title(), {}};
}

QDBusObjectPath TrayStatusItem::menu() const
{
    return QDBusObjectPath(kMenuPath);
}

void TrayStatusItem::Activate(int, int)
{
    Q_EMIT activated();
}

void TrayStatusItem::SecondaryActivate(int, int)
{
    Q_EMIT activated();
}

void TrayStatusItem::ContextMenu(int, int)
{
    // Hosts show the menu at Menu themselves; this is for those that cannot.
}

void TrayStatusItem::Scroll(int, const QString &) {}

QVariantMap TrayMenu::propertiesOf(int id) const
{
    switch (id) {
    case 0: return {{u"children-display"_s, u"submenu"_s}};
    case Open: return {{u"label"_s, QCoreApplication::translate("TrayMenu", "Open Callie")}};
    case Sync: return {{u"label"_s, QCoreApplication::translate("TrayMenu", "Sync now")}};
    case Separator: return {{u"type"_s, u"separator"_s}};
    case Quit: return {{u"label"_s, QCoreApplication::translate("TrayMenu", "Quit Callie")}};
    default: return {};
    }
}

uint TrayMenu::GetLayout(int parentId, int recursionDepth, const QStringList &,
                         DBusMenuEntry &layout)
{
    layout = {parentId, propertiesOf(parentId), {}};
    // The menu is one level deep: only the root has entries.
    if (parentId == 0 && recursionDepth != 0) {
        for (int id : {Open, Sync, Separator, Quit})
            layout.children.append(QVariant::fromValue(DBusMenuEntry{id, propertiesOf(id), {}}));
    }
    return 1;
}

QList<DBusMenuProperties> TrayMenu::GetGroupProperties(const QList<int> &ids, const QStringList &)
{
    QList<DBusMenuProperties> result;
    for (int id : ids)
        result.append({id, propertiesOf(id)});
    return result;
}

QDBusVariant TrayMenu::GetProperty(int id, const QString &name)
{
    return QDBusVariant(propertiesOf(id).value(name));
}

void TrayMenu::Event(int id, const QString &eventId, const QDBusVariant &, uint)
{
    if (eventId != u"clicked" || (id != Open && id != Sync && id != Quit))
        return;
    // After the reply, so quitting does not cut it off.
    QMetaObject::invokeMethod(this, [this, id] { Q_EMIT picked(id); }, Qt::QueuedConnection);
}

bool TrayMenu::AboutToShow(int)
{
    return false;
}

TrayItem::TrayItem(const QDBusConnection &bus, const QString &watcher, QObject *parent)
    : QObject(parent), m_bus(bus), m_watcher(watcher),
      m_service(u"org.kde.StatusNotifierItem-%1-1"_s.arg(QCoreApplication::applicationPid())),
      m_item(new TrayStatusItem(this)), m_menu(new TrayMenu(this)),
      m_watching(
          new QDBusServiceWatcher(watcher, bus, QDBusServiceWatcher::WatchForRegistration, this))
{
    registerTypes();
    connect(m_item, &TrayStatusItem::activated, this, &TrayItem::openRequested);
    connect(m_menu, &TrayMenu::picked, this, [this](int entry) {
        if (entry == TrayMenu::Open)
            Q_EMIT openRequested();
        else if (entry == TrayMenu::Sync)
            Q_EMIT syncRequested();
        else if (entry == TrayMenu::Quit)
            Q_EMIT quitRequested();
    });
    // A panel started, or restarted, after Callie learns of the entry anew.
    connect(m_watching, &QDBusServiceWatcher::serviceRegistered, this, [this] {
        if (m_visible)
            announce();
    });
}

TrayItem::~TrayItem()
{
    setVisible(false);
}

void TrayItem::setVisible(bool visible)
{
    if (visible == m_visible || !m_bus.isConnected())
        return;
    m_visible = visible;
    const auto exports = QDBusConnection::ExportScriptableSlots |
                         QDBusConnection::ExportScriptableSignals |
                         QDBusConnection::ExportAllProperties;
    if (visible) {
        m_bus.registerObject(kItemPath, m_item, exports);
        m_bus.registerObject(kMenuPath, m_menu, exports);
        m_bus.registerService(m_service);
        announce();
    } else {
        m_bus.unregisterService(m_service);
        m_bus.unregisterObject(kMenuPath);
        m_bus.unregisterObject(kItemPath);
    }
}

void TrayItem::announce()
{
    QDBusMessage call = QDBusMessage::createMethodCall(m_watcher, u"/StatusNotifierWatcher"_s,
                                                       u"org.kde.StatusNotifierWatcher"_s,
                                                       u"RegisterStatusNotifierItem"_s);
    call << m_service;
    // No host, such as plain GNOME, is not an error: there is just no tray.
    m_bus.callWithCallback(call, this, nullptr, nullptr);
    qCDebug(lcUi) << "tray entry offered as" << m_service;
}

} // namespace callie
