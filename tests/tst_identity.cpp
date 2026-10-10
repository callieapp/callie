#include "callie/Identity.h"
#include "callie/Settings.h"

#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestIdentity : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void developmentBuildsStandApart();
    void thisBuildIsWhatCMakeSays();
};

void TestIdentity::developmentBuildsStandApart()
{
    // An installed Callie and a development build never share a name or folder.
    const identity::Names plain = identity::namesFor(false);
    QCOMPARE(plain.appId, u"org.callieapp.Callie"_s);
    QCOMPARE(plain.dirName, u"callie"_s);
    QCOMPARE(plain.displayName, u"Callie"_s);
    const identity::Names devel = identity::namesFor(true);
    QCOMPARE(devel.appId, u"org.callieapp.Callie.Devel"_s);
    QCOMPARE(devel.dirName, u"callie-devel"_s);
    QCOMPARE(devel.displayName, u"Callie Devel"_s);
}

void TestIdentity::thisBuildIsWhatCMakeSays()
{
    QCOMPARE(identity::isDevel(), bool(CALLIE_EXPECT_DEVEL));
    QCOMPARE(identity::appId(), identity::namesFor(CALLIE_EXPECT_DEVEL).appId);
    QVERIFY(Settings::defaultPath().endsWith(u'/' + identity::dirName() + u"/settings.ini"_s));
}

QTEST_GUILESS_MAIN(TestIdentity)
#include "tst_identity.moc"
