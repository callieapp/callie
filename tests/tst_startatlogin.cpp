#include "StartAtLogin.h"

#include "callie/Autostart.h"
#include "callie/Settings.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

/// The desktop's Background portal, answering every request with `answer`.
class FakePortal : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Background")

public:
    explicit FakePortal(QDBusConnection bus) : m_bus(std::move(bus)) {}

    uint answer = 0;
    QVariantMap asked;

public Q_SLOTS:
    Q_SCRIPTABLE QDBusObjectPath RequestBackground(const QString &, const QVariantMap &options)
    {
        asked = options;
        QString sender = message().service().mid(1);
        sender.replace(u'.', u'_');
        const QString path = u"/org/freedesktop/portal/desktop/request/%1/%2"_s.arg(
            sender, options.value(u"handle_token"_s).toString());
        // Answered after the call returns, as the user decides later.
        QMetaObject::invokeMethod(
            this,
            [this, path] {
                QDBusMessage response = QDBusMessage::createSignal(
                    path, u"org.freedesktop.portal.Request"_s, u"Response"_s);
                response << answer << QVariantMap{{u"autostart"_s, answer == 0}};
                m_bus.send(response);
            },
            Qt::QueuedConnection);
        return QDBusObjectPath(path);
    }

private:
    QDBusConnection m_bus;
};

} // namespace

class TestStartAtLogin : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void followsKeepRunning();
    void failureIsShownUntilResolved();
    void existingEntryIsLeftAlone();
    void aFlatpakAsksThePortal();
};

void TestStartAtLogin::followsKeepRunning()
{
    QTemporaryDir dir;
    Settings settings(dir.filePath(u"settings.ini"_s));
    settings.setKeepRunning(true);
    const QString entry = dir.filePath(u"autostart/app.desktop"_s);
    Autostart autostart(u"org.example.App"_s, u"/usr/bin/callie-gui"_s, entry);
    StartAtLogin *startAtLogin = StartAtLogin::instance();
    startAtLogin->setup(&autostart, &settings);
    QVERIFY(startAtLogin->available());

    startAtLogin->setEnabled(true);
    QVERIFY(startAtLogin->enabled());
    QVERIFY(QFile::exists(entry));

    // No longer running in the background, so nothing to start at login.
    settings.setKeepRunning(false);
    QVERIFY(!startAtLogin->enabled());
    QVERIFY(!QFile::exists(entry));
    QVERIFY(startAtLogin->error().isEmpty());
    startAtLogin->setup(nullptr, nullptr);
}

void TestStartAtLogin::failureIsShownUntilResolved()
{
    QTemporaryDir dir;
    Settings settings(dir.filePath(u"settings.ini"_s));
    settings.setKeepRunning(true);
    // A file where the autostart folder should be, so the entry cannot be written.
    QFile blocker(dir.filePath(u"autostart"_s));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    Autostart autostart(u"org.example.App"_s, u"/usr/bin/callie-gui"_s,
                        dir.filePath(u"autostart/app.desktop"_s));
    StartAtLogin *startAtLogin = StartAtLogin::instance();
    startAtLogin->setup(&autostart, &settings);

    startAtLogin->setEnabled(true);
    QVERIFY(!startAtLogin->enabled());
    QVERIFY(!startAtLogin->error().isEmpty());

    // Asking for what is already so clears the old complaint.
    QSignalSpy changed(startAtLogin, &StartAtLogin::changed);
    startAtLogin->setEnabled(false);
    QVERIFY(startAtLogin->error().isEmpty());
    QCOMPARE(changed.size(), 1);
    startAtLogin->setup(nullptr, nullptr);
}

void TestStartAtLogin::existingEntryIsLeftAlone()
{
    // An entry the user put there, while Callie is not set to keep running.
    QTemporaryDir dir;
    Settings settings(dir.filePath(u"settings.ini"_s));
    const QString entry = dir.filePath(u"app.desktop"_s);
    Autostart autostart(u"org.example.App"_s, u"/usr/bin/callie-gui"_s, entry);
    QVERIFY(autostart.setEnabled(true));

    StartAtLogin::instance()->setup(&autostart, &settings);
    QVERIFY(QFile::exists(entry));
    QVERIFY(StartAtLogin::instance()->enabled());
    StartAtLogin::instance()->setup(nullptr, nullptr);
}

void TestStartAtLogin::aFlatpakAsksThePortal()
{
    if (!QDBusConnection::sessionBus().isConnected())
        QSKIP("no session bus");
    QDBusConnection desktop =
        QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"portal"_s);
    FakePortal portal(desktop);
    const QString name = u"org.callieapp.TestPortal%1"_s.arg(QCoreApplication::applicationPid());
    QVERIFY(desktop.registerObject(u"/org/freedesktop/portal/desktop"_s, &portal,
                                   QDBusConnection::ExportScriptableSlots));
    QVERIFY(desktop.registerService(name));

    QTemporaryDir dir;
    Settings settings(dir.filePath(u"settings.ini"_s));
    settings.setKeepRunning(true);
    Autostart autostart(u"org.example.App"_s, u"/app/bin/callie-gui"_s,
                        dir.filePath(u"autostart/app.desktop"_s));
    StartAtLogin *startAtLogin = StartAtLogin::instance();
    startAtLogin->setup(&autostart, &settings);
    startAtLogin->setPortal(name);
    qputenv("CALLIE_FLATPAK", "1");

    // Allowed: the portal starts Callie, in the background, at login.
    startAtLogin->setEnabled(true);
    QTRY_COMPARE(portal.asked.value(u"autostart"_s).toBool(), true);
    QCOMPARE(portal.asked.value(u"commandline"_s).toStringList(),
             (QStringList{u"callie-gui"_s, u"--background"_s}));
    QTest::qWait(100);
    QVERIFY(startAtLogin->enabled());
    QVERIFY(startAtLogin->error().isEmpty());

    // Turned off, the portal hears that too.
    startAtLogin->setEnabled(false);
    QTRY_COMPARE(portal.asked.value(u"autostart"_s).toBool(), false);
    QVERIFY(!startAtLogin->enabled());

    // Refused: the setting goes back off, saying why.
    portal.answer = 1;
    startAtLogin->setEnabled(true);
    QTRY_VERIFY(!startAtLogin->enabled());
    QVERIFY(startAtLogin->error().contains(u"did not allow"_s));

    qputenv("CALLIE_FLATPAK", "0");
    startAtLogin->setPortal(u"org.freedesktop.portal.Desktop"_s);
    startAtLogin->setup(nullptr, nullptr);
}

QTEST_GUILESS_MAIN(TestStartAtLogin)
#include "tst_startatlogin.moc"
