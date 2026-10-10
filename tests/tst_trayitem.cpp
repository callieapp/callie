#include "TrayItem.h"
#include "callie/Identity.h"

#include <QCoreApplication>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusReply>
#include <QSignalSpy>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

/// The panel's side: takes the entries offered to it.
class FakeWatcher : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")

public:
    QStringList offered;

public Q_SLOTS:
    Q_SCRIPTABLE void RegisterStatusNotifierItem(const QString &service) { offered << service; }
};

} // namespace

class TestTrayItem : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void panelShowsAndDrivesTheEntry();
};

void TestTrayItem::panelShowsAndDrivesTheEntry()
{
    if (!QDBusConnection::sessionBus().isConnected())
        QSKIP("no session bus");
    // The panel runs on a connection of its own, like another process, under
    // a name of its own, so a real panel is left alone.
    QDBusConnection panel = QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"panel"_s);
    const QString watcherName =
        u"org.callieapp.TestWatcher%1"_s.arg(QCoreApplication::applicationPid());
    FakeWatcher watcher;
    QVERIFY(panel.registerObject(u"/StatusNotifierWatcher"_s, &watcher,
                                 QDBusConnection::ExportScriptableSlots));
    QVERIFY(panel.registerService(watcherName));

    TrayItem tray(QDBusConnection::sessionBus(), watcherName);
    tray.setVisible(true);
    QTRY_COMPARE(watcher.offered, QStringList{tray.serviceName()});

    // The entry answers from this process, so its event loop has to run.
    const auto ask = [&panel](const QDBusMessage &message) {
        QDBusPendingCall pending = panel.asyncCall(message);
        // A call that never ends comes back as an error, which the checks below catch.
        if (!QTest::qWaitFor([&pending] { return pending.isFinished(); }))
            return QDBusMessage();
        return pending.reply();
    };
    const auto property = [&](const QString &path, const QString &interface, const QString &name) {
        QDBusMessage get = QDBusMessage::createMethodCall(
            tray.serviceName(), path, u"org.freedesktop.DBus.Properties"_s, u"Get"_s);
        get << interface << name;
        const QDBusReply<QDBusVariant> reply = ask(get);
        return reply.isValid() ? reply.value().variant() : QVariant();
    };
    QCOMPARE(
        property(u"/StatusNotifierItem"_s, u"org.kde.StatusNotifierItem"_s, u"Title"_s).toString(),
        identity::displayName());
    QCOMPARE(property(u"/StatusNotifierItem"_s, u"org.kde.StatusNotifierItem"_s, u"Menu"_s)
                 .value<QDBusObjectPath>()
                 .path(),
             u"/MenuBar"_s);
    // A name from the icon theme, or the logo's pixels when it has none.
    const QString icon =
        property(u"/StatusNotifierItem"_s, u"org.kde.StatusNotifierItem"_s, u"IconName"_s)
            .toString();
    const auto pixmaps = qdbus_cast<QList<DBusTrayImage>>(
        property(u"/StatusNotifierItem"_s, u"org.kde.StatusNotifierItem"_s, u"IconPixmap"_s)
            .value<QDBusArgument>());
    QVERIFY(!icon.isEmpty() || !pixmaps.isEmpty());
    for (const DBusTrayImage &image : pixmaps)
        QCOMPARE(image.pixels.size(), qsizetype(image.width) * image.height * 4);

    // The menu: open, sync, a separator and quit.
    QDBusMessage layout = QDBusMessage::createMethodCall(
        tray.serviceName(), u"/MenuBar"_s, u"com.canonical.dbusmenu"_s, u"GetLayout"_s);
    layout << 0 << -1 << QStringList();
    const QDBusMessage laidOut = ask(layout);
    QCOMPARE(laidOut.type(), QDBusMessage::ReplyMessage);
    const auto root = qdbus_cast<DBusMenuEntry>(laidOut.arguments().at(1));
    QStringList labels;
    for (const QVariant &child : root.children) {
        const auto entry = qdbus_cast<DBusMenuEntry>(child.value<QDBusArgument>());
        labels << entry.properties.value(u"label"_s, entry.properties.value(u"type"_s)).toString();
    }
    QCOMPARE(labels,
             (QStringList{u"Open Callie"_s, u"Sync now"_s, u"separator"_s, u"Quit Callie"_s}));

    // A click opens the window; the menu's entries ask for what they say.
    QSignalSpy opened(&tray, &TrayItem::openRequested);
    QSignalSpy synced(&tray, &TrayItem::syncRequested);
    QSignalSpy quit(&tray, &TrayItem::quitRequested);
    ask(QDBusMessage::createMethodCall(tray.serviceName(), u"/StatusNotifierItem"_s,
                                       u"org.kde.StatusNotifierItem"_s, u"Activate"_s)
        << 0 << 0);
    QTRY_COMPARE(opened.size(), 1);
    const auto click = [&](int id) {
        ask(QDBusMessage::createMethodCall(tray.serviceName(), u"/MenuBar"_s,
                                           u"com.canonical.dbusmenu"_s, u"Event"_s)
            << id << u"clicked"_s << QVariant::fromValue(QDBusVariant(0)) << uint(0));
    };
    click(TrayMenu::Sync);
    QTRY_COMPARE(synced.size(), 1);
    click(TrayMenu::Quit);
    QTRY_COMPARE(quit.size(), 1);

    // Hidden, it leaves the bus.
    tray.setVisible(false);
    QTRY_VERIFY(!panel.interface()->isServiceRegistered(tray.serviceName()));

    panel.unregisterService(watcherName);
    QDBusConnection::disconnectFromBus(u"panel"_s);
}

QTEST_GUILESS_MAIN(TestTrayItem)
#include "tst_trayitem.moc"
