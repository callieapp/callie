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
};

void TestIdentity::developmentBuildsStandApart()
{
    // An installed Callie and a development build never share a name or folder.
    if (identity::isDevel()) {
        QCOMPARE(identity::appId(), u"org.callieapp.Callie.Devel"_s);
        QCOMPARE(identity::dirName(), u"callie-devel"_s);
        QCOMPARE(identity::displayName(), u"Callie Devel"_s);
    } else {
        QCOMPARE(identity::appId(), u"org.callieapp.Callie"_s);
        QCOMPARE(identity::dirName(), u"callie"_s);
        QCOMPARE(identity::displayName(), u"Callie"_s);
    }
    QVERIFY(Settings::defaultPath().endsWith(u'/' + identity::dirName() + u"/settings.ini"_s));
}

QTEST_GUILESS_MAIN(TestIdentity)
#include "tst_identity.moc"
