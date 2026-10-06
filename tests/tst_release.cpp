#include "EventModelForeign.h"
#include "ReleaseInfo.h"

#include "callie/Settings.h"

#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestRelease : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void notesComeFromTheBundledChangelog();
    void updateNoticeShowsOncePerNewVersion();
};

void TestRelease::notesComeFromTheBundledChangelog()
{
    const ReleaseInfo release;
    QVERIFY(!release.notesVersion().isEmpty());
    QVERIFY(release.notesDate().isValid());
    QVERIFY(release.notes().contains(u"### New"_s));
}

void TestRelease::updateNoticeShowsOncePerNewVersion()
{
    QTemporaryDir dir;
    Settings settings(dir.filePath(u"settings.ini"_s));
    SettingsForeign::s_instance = &settings;
    ReleaseInfo release;

    // A first run only records the version.
    QCoreApplication::setApplicationVersion(u"0.1.0"_s);
    QVERIFY(!release.takeUpdateNotice());
    QCOMPARE(settings.lastSeenVersion(), u"0.1.0"_s);
    QVERIFY(!release.takeUpdateNotice());

    // A new version says so once.
    QCoreApplication::setApplicationVersion(u"0.2.0"_s);
    QVERIFY(release.takeUpdateNotice());
    QVERIFY(!release.takeUpdateNotice());
    SettingsForeign::s_instance = nullptr;
}

QTEST_GUILESS_MAIN(TestRelease)
#include "tst_release.moc"
