#include "FakeHttpServer.h"
#include "FakeTokenStore.h"

#include "callie/GoogleTokenProvider.h"

#include <QTest>
#include <QUrlQuery>

using namespace callie;

namespace {

const GoogleClientConfig kClient{QStringLiteral("test-id"), QStringLiteral("test-secret")};
const Account kAccount{QStringLiteral("google"), QStringLiteral("me@example.com")};

struct Outcome
{
    QString token;
    QString error;
    bool done = false;
};

} // namespace

class TestGoogleTokenProvider : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();

    void refreshesWithStoredToken();
    void cachedTokenIsReused();
    void concurrentRequestsShareOneRefresh();
    void tokenNearExpiryIsRefreshed();
    void invalidatedTokenIsRefreshed();
    void rotatedRefreshTokenIsStored();
    void missingStoredTokenFails();
    void keyringErrorFails();
    void revokedRefreshTokenFails();
    void cachedResultIsNotSynchronous();

private:
    Outcome request();

    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<FakeTokenStore> m_store;
    std::unique_ptr<GoogleTokenProvider> m_provider;
};

void TestGoogleTokenProvider::init()
{
    m_server = std::make_unique<FakeHttpServer>();
    m_server->respond(200, R"({"access_token":"at-1","expires_in":3600,"token_type":"Bearer"})");
    m_store = std::make_unique<FakeTokenStore>();
    m_store->secrets.insert(kAccount.id, QStringLiteral("rt-1"));
    m_provider = std::make_unique<GoogleTokenProvider>(kClient, *m_store);
    m_provider->setTokenUrl(m_server->url(QStringLiteral("/token")));
}

Outcome TestGoogleTokenProvider::request()
{
    Outcome outcome;
    m_provider->accessToken(kAccount, [&](const QString &token, const QString &error) {
        outcome = {token, error, true};
    });
    [&] { QTRY_VERIFY_WITH_TIMEOUT(outcome.done, 5000); }();
    return outcome;
}

void TestGoogleTokenProvider::refreshesWithStoredToken()
{
    const Outcome outcome = request();

    QCOMPARE(outcome.error, QString());
    QCOMPARE(outcome.token, QStringLiteral("at-1"));
    QCOMPARE(m_server->requests.size(), 1);
    const QUrlQuery body(QString::fromUtf8(m_server->requests.first().body));
    QCOMPARE(body.queryItemValue(QStringLiteral("grant_type")), QStringLiteral("refresh_token"));
    QCOMPARE(body.queryItemValue(QStringLiteral("refresh_token")), QStringLiteral("rt-1"));
}

void TestGoogleTokenProvider::cachedTokenIsReused()
{
    request();
    const Outcome second = request();

    QCOMPARE(second.token, QStringLiteral("at-1"));
    QCOMPARE(m_server->requests.size(), 1);
    QCOMPARE(m_store->reads, 1);
}

void TestGoogleTokenProvider::concurrentRequestsShareOneRefresh()
{
    QStringList tokens;
    for (int i = 0; i < 3; ++i) {
        m_provider->accessToken(
            kAccount, [&](const QString &token, const QString &) { tokens.append(token); });
    }

    QTRY_COMPARE_WITH_TIMEOUT(tokens.size(), 3, 5000);
    QCOMPARE(tokens, QStringList(3, QStringLiteral("at-1")));
    QCOMPARE(m_server->requests.size(), 1);
    QCOMPARE(m_store->reads, 1);
}

void TestGoogleTokenProvider::tokenNearExpiryIsRefreshed()
{
    m_server->enqueue(200, R"({"access_token":"short","expires_in":30,"token_type":"Bearer"})");
    QCOMPARE(request().token, QStringLiteral("short"));

    QCOMPARE(request().token, QStringLiteral("at-1"));
    QCOMPARE(m_server->requests.size(), 2);
}

void TestGoogleTokenProvider::invalidatedTokenIsRefreshed()
{
    m_server->enqueue(200,
                      R"({"access_token":"rejected","expires_in":3600,"token_type":"Bearer"})");
    QCOMPARE(request().token, QStringLiteral("rejected"));

    m_provider->invalidate(kAccount);
    QCOMPARE(request().token, QStringLiteral("at-1"));
}

void TestGoogleTokenProvider::rotatedRefreshTokenIsStored()
{
    m_server->respond(200, R"({"access_token":"at-1","refresh_token":"rt-2","expires_in":3600,
                               "token_type":"Bearer"})");
    request();

    QTRY_COMPARE_WITH_TIMEOUT(m_store->secrets.value(kAccount.id), QStringLiteral("rt-2"), 2000);
}

void TestGoogleTokenProvider::missingStoredTokenFails()
{
    m_store->secrets.clear();
    const Outcome outcome = request();

    QVERIFY(outcome.token.isEmpty());
    QVERIFY(outcome.error.contains(kAccount.id));
    QCOMPARE(m_server->requests.size(), 0);
}

void TestGoogleTokenProvider::keyringErrorFails()
{
    m_store->failWith = QStringLiteral("keyring is locked");
    const Outcome outcome = request();

    QVERIFY(outcome.error.contains(QStringLiteral("keyring is locked")));
    QCOMPARE(m_server->requests.size(), 0);
}

void TestGoogleTokenProvider::revokedRefreshTokenFails()
{
    m_server->respond(400, R"({"error":"invalid_grant",
                               "error_description":"Token has been expired or revoked."})");
    const Outcome outcome = request();

    QVERIFY(outcome.token.isEmpty());
    QVERIFY(!outcome.error.isEmpty());

    // A failure is not cached; the next request tries again.
    m_server->respond(200, R"({"access_token":"at-1","expires_in":3600,"token_type":"Bearer"})");
    QCOMPARE(request().token, QStringLiteral("at-1"));
}

void TestGoogleTokenProvider::cachedResultIsNotSynchronous()
{
    request();

    bool called = false;
    m_provider->accessToken(kAccount, [&](const QString &, const QString &) { called = true; });
    QVERIFY(!called);
    QTRY_VERIFY_WITH_TIMEOUT(called, 2000);
}

QTEST_GUILESS_MAIN(TestGoogleTokenProvider)
#include "tst_googletokenprovider.moc"
