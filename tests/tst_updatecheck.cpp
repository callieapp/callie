#include "FakeHttpServer.h"

#include "callie/UpdateCheck.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestUpdateCheck : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void remembersTheLatestRelease();
    void asksAtMostOnceADay();
    void aFailedCheckKeepsWhatWasKnown();
    void notWantedForDevelopmentOrWhenTurnedOff();

private:
    std::unique_ptr<UpdateCheck> make();
    static QByteArray latest(const QString &version, bool prerelease = false);

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<FakeHttpServer> m_github;
    QDateTime m_now;
};

QByteArray TestUpdateCheck::latest(const QString &version, bool prerelease)
{
    return QJsonDocument(QJsonObject{{u"tag_name"_s, u"v"_s + version},
                                     {u"prerelease"_s, prerelease},
                                     {u"html_url"_s, u"https://example.com/v"_s + version}})
        .toJson();
}

void TestUpdateCheck::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_github = std::make_unique<FakeHttpServer>();
    m_now = QDateTime(QDate(2026, 10, 11), QTime(9, 0), QTimeZone::UTC);
}

std::unique_ptr<UpdateCheck> TestUpdateCheck::make()
{
    auto check = std::make_unique<UpdateCheck>(m_dir->filePath(u"cache/latest-release.json"_s));
    check->setApi(m_github->url(QString()));
    check->setNow([this] { return m_now; });
    return check;
}

void TestUpdateCheck::remembersTheLatestRelease()
{
    m_github->respond(200, latest(u"0.3.1"_s));
    std::unique_ptr<UpdateCheck> check = make();
    QVERIFY(!check->newerThan(QVersionNumber(0, 3, 0)));
    QSignalSpy checked(check.get(), &UpdateCheck::checked);
    check->check();
    QVERIFY(checked.wait());
    QCOMPARE(m_github->requests.last().target, QByteArray("/releases/latest"));

    // Read back by another, as the CLI and the app each are.
    const std::optional<Release> newer = make()->newerThan(QVersionNumber(0, 3, 0));
    QVERIFY(newer);
    QCOMPARE(newer->version, QVersionNumber(0, 3, 1));
    QCOMPARE(newer->page, QUrl(u"https://example.com/v0.3.1"_s));
    QVERIFY(!make()->newerThan(QVersionNumber(0, 3, 1)));
    QVERIFY(!make()->newerThan(QVersionNumber(0, 4, 0)));
}

void TestUpdateCheck::asksAtMostOnceADay()
{
    std::unique_ptr<UpdateCheck> check = make();
    QVERIFY(check->due());
    check->markAsked();
    QVERIFY(!make()->due());
    m_now = m_now.addSecs(23 * 3600);
    QVERIFY(!make()->due());
    m_now = m_now.addSecs(3600);
    QVERIFY(make()->due());
    // A clock set back does not keep it from asking.
    m_now = m_now.addDays(-3);
    QVERIFY(make()->due());
}

void TestUpdateCheck::aFailedCheckKeepsWhatWasKnown()
{
    m_github->respond(200, latest(u"0.3.1"_s));
    std::unique_ptr<UpdateCheck> check = make();
    QSignalSpy checked(check.get(), &UpdateCheck::checked);
    check->check();
    QVERIFY(checked.wait());

    // An error, or a pre-release, leaves 0.3.1 known, and waits a day to ask again.
    m_now = m_now.addDays(2);
    m_github->respond(500, "{}");
    check->check();
    QVERIFY(checked.wait());
    m_github->respond(200, latest(u"0.4.0"_s, true));
    check->check();
    QVERIFY(checked.wait());
    QCOMPARE(make()->newerThan(QVersionNumber(0, 3, 0))->version, QVersionNumber(0, 3, 1));
    QVERIFY(!make()->due());
}

void TestUpdateCheck::notWantedForDevelopmentOrWhenTurnedOff()
{
    std::unique_ptr<UpdateCheck> check = make();
    check->setDevel(false);
    QVERIFY(check->wanted(true));
    QVERIFY(!check->wanted(false));
    check->setDevel(true);
    QVERIFY(!check->wanted(true));
    check->setDevel(false);
    qputenv("CALLIE_NO_UPDATE_CHECK", "1");
    QVERIFY(!check->wanted(true));
    qunsetenv("CALLIE_NO_UPDATE_CHECK");
}

QTEST_MAIN(TestUpdateCheck)
#include "tst_updatecheck.moc"
