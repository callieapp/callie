#include "WindowButtons.h"

#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestWindowButtons : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void gnomeDefaultHasOnlyClose();
    void gnomeLayoutKeepsOrderAndSides();
    void gnomeIgnoresUnknownButtons();
    void kdeDefaultPutsThreeOnTheRight();
    void kdeLettersMapToButtons();
    void readsKwinrcOnKde();
    void readsGnomeSettingWithoutBlocking();
    void keepsTheDefaultWithoutGsettings();
};

void TestWindowButtons::gnomeDefaultHasOnlyClose()
{
    const auto layout = WindowButtons::fromGnome(u"'appmenu:close'\n"_s);
    QCOMPARE(layout.left, QStringList());
    QCOMPARE(layout.right, QStringList{u"close"_s});
}

void TestWindowButtons::gnomeLayoutKeepsOrderAndSides()
{
    const auto layout = WindowButtons::fromGnome(u"'close,minimize,maximize:'"_s);
    QCOMPARE(layout.left, (QStringList{u"close"_s, u"minimize"_s, u"maximize"_s}));
    QCOMPARE(layout.right, QStringList());
}

void TestWindowButtons::gnomeIgnoresUnknownButtons()
{
    const auto layout = WindowButtons::fromGnome(u"icon,menu:spacer,minimize,maximize,close"_s);
    QCOMPARE(layout.left, QStringList());
    QCOMPARE(layout.right, (QStringList{u"minimize"_s, u"maximize"_s, u"close"_s}));
}

void TestWindowButtons::kdeDefaultPutsThreeOnTheRight()
{
    const auto layout = WindowButtons::fromKde(u"MS"_s, u"HIAX"_s);
    QCOMPARE(layout.left, QStringList());
    QCOMPARE(layout.right, (QStringList{u"minimize"_s, u"maximize"_s, u"close"_s}));
}

void TestWindowButtons::kdeLettersMapToButtons()
{
    const auto layout = WindowButtons::fromKde(u"XIA"_s, u"N"_s);
    QCOMPARE(layout.left, (QStringList{u"close"_s, u"minimize"_s, u"maximize"_s}));
    QCOMPARE(layout.right, QStringList());
}

namespace {

void write(const QString &path, const QByteArray &content, bool executable = false)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
    file.close();
    if (executable)
        file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
}

} // namespace

void TestWindowButtons::readsKwinrcOnKde()
{
    QTemporaryDir config;
    write(config.filePath(u"kwinrc"_s),
          "[org.kde.kdecoration2]\nButtonsOnLeft=XA\nButtonsOnRight=\n");
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    qputenv("XDG_CURRENT_DESKTOP", "KDE");

    const WindowButtons buttons;

    QCOMPARE(buttons.left(), (QStringList{u"close"_s, u"maximize"_s}));
    QCOMPARE(buttons.right(), QStringList());
}

void TestWindowButtons::readsGnomeSettingWithoutBlocking()
{
    QTemporaryDir bin;
    write(bin.filePath(u"gsettings"_s), "#!/bin/sh\nsleep 0.2\necho \"'close:'\"\n", true);
    const QByteArray path = qgetenv("PATH");
    qputenv("PATH", bin.path().toUtf8() + ':' + path);
    qputenv("XDG_CURRENT_DESKTOP", "GNOME");

    QElapsedTimer timer;
    timer.start();
    WindowButtons buttons;
    // The default stands until the answer arrives, and creating it did not wait.
    QVERIFY(timer.elapsed() < 150);
    QCOMPARE(buttons.right(), (QStringList{u"minimize"_s, u"maximize"_s, u"close"_s}));

    QSignalSpy changed(&buttons, &WindowButtons::changed);
    QVERIFY(changed.wait(3000));
    qputenv("PATH", path);
    QCOMPARE(buttons.left(), QStringList{u"close"_s});
    QCOMPARE(buttons.right(), QStringList());
}

void TestWindowButtons::keepsTheDefaultWithoutGsettings()
{
    QTemporaryDir empty;
    const QByteArray path = qgetenv("PATH");
    qputenv("PATH", empty.path().toUtf8());
    qputenv("XDG_CURRENT_DESKTOP", "GNOME");

    WindowButtons buttons;
    QSignalSpy changed(&buttons, &WindowButtons::changed);
    QTest::qWait(300);
    qputenv("PATH", path);

    QCOMPARE(changed.size(), 0);
    QCOMPARE(buttons.right(), (QStringList{u"minimize"_s, u"maximize"_s, u"close"_s}));
}

QTEST_GUILESS_MAIN(TestWindowButtons)
#include "tst_windowbuttons.moc"
