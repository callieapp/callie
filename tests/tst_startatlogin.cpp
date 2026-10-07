#include "StartAtLogin.h"

#include "callie/Autostart.h"
#include "callie/Settings.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestStartAtLogin : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void followsKeepRunning();
    void failureIsShownUntilResolved();
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

QTEST_GUILESS_MAIN(TestStartAtLogin)
#include "tst_startatlogin.moc"
