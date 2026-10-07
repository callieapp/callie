#include "callie/Autostart.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestAutostart : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void entryStartsInTheBackground();
    void programWithSpacesIsQuoted();
    void turningOffRemovesTheEntry();
};

void TestAutostart::entryStartsInTheBackground()
{
    QTemporaryDir dir;
    // The autostart folder may not exist yet.
    const QString path = dir.filePath(u"autostart/org.example.App.desktop"_s);
    Autostart autostart(u"org.example.App"_s, u"/usr/bin/callie-gui"_s, path);
    QVERIFY(!autostart.isEnabled());

    QVERIFY2(autostart.setEnabled(true), qPrintable(autostart.errorString()));
    QVERIFY(autostart.isEnabled());
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString entry = QString::fromUtf8(file.readAll());
    QVERIFY(entry.startsWith(u"[Desktop Entry]\n"_s));
    QVERIFY(entry.contains(u"\nExec=/usr/bin/callie-gui --background\n"_s));
    QVERIFY(entry.contains(u"\nIcon=org.example.App\n"_s));
}

void TestAutostart::programWithSpacesIsQuoted()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(u"app.desktop"_s);
    Autostart autostart(u"org.example.App"_s, u"/home/me/my builds/callie-gui"_s, path);
    QVERIFY(autostart.setEnabled(true));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(QString::fromUtf8(file.readAll())
                .contains(u"\nExec=\"/home/me/my builds/callie-gui\" --background\n"_s));
}

void TestAutostart::turningOffRemovesTheEntry()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(u"app.desktop"_s);
    Autostart autostart(u"org.example.App"_s, u"/usr/bin/callie-gui"_s, path);
    QVERIFY(autostart.setEnabled(true));
    QVERIFY(autostart.setEnabled(false));
    QVERIFY(!QFile::exists(path));
    // Already off is fine.
    QVERIFY(autostart.setEnabled(false));
}

QTEST_GUILESS_MAIN(TestAutostart)
#include "tst_autostart.moc"
