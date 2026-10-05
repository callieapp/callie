#include "GoogleAuthDriver.h"

#include "callie/GoogleAuth.h"
#include "callie/GoogleCalendarApi.h"
#include "callie/GoogleClientConfig.h"

#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTest>
#include <QUrlQuery>

using namespace callie;

namespace {

const GoogleClientConfig kClient{QStringLiteral("test-id"), QStringLiteral("test-secret")};

QUrlQuery formBody(const QByteArray &body)
{
    return QUrlQuery(QString::fromUtf8(body));
}

QString formValue(const QUrlQuery &query, const char *key)
{
    return query.queryItemValue(QString::fromLatin1(key), QUrl::FullyDecoded);
}

} // namespace

class TestGoogleAuth : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void environmentReplacesBuiltInClient();
    void environmentSecretAloneIsIgnored();
    void authorizationUrlRequestsOfflinePkceAccess();
    void callbackListensOnLoopbackOnly();
    void codeExchangeSendsVerifierMatchingChallenge();
    void mismatchedStateIsNotExchanged();
    void missingRefreshTokenFails();
    void refreshKeepsExistingRefreshToken();
    void primaryCalendarIdSendsBearerToken();
    void apiErrorMessageIsReported();
};

void TestGoogleAuth::environmentReplacesBuiltInClient()
{
    QProcessEnvironment env;
    env.insert(QStringLiteral("CALLIE_GOOGLE_CLIENT_ID"), QStringLiteral("env-id"));
    env.insert(QStringLiteral("CALLIE_GOOGLE_CLIENT_SECRET"), QStringLiteral("env-secret"));

    const GoogleClientConfig resolved = GoogleClientConfig::resolve(kClient, env);
    QCOMPARE(resolved.clientId, QStringLiteral("env-id"));
    QCOMPARE(resolved.clientSecret, QStringLiteral("env-secret"));
}

void TestGoogleAuth::environmentSecretAloneIsIgnored()
{
    QProcessEnvironment env;
    env.insert(QStringLiteral("CALLIE_GOOGLE_CLIENT_SECRET"), QStringLiteral("env-secret"));

    const GoogleClientConfig resolved = GoogleClientConfig::resolve(kClient, env);
    QCOMPARE(resolved.clientId, kClient.clientId);
    QCOMPARE(resolved.clientSecret, kClient.clientSecret);
}

void TestGoogleAuth::authorizationUrlRequestsOfflinePkceAccess()
{
    FakeHttpServer tokenServer;
    GoogleAuth auth(kClient);
    const QUrl url = GoogleAuthDriver::startAuthorization(auth, tokenServer);
    QVERIFY(url.isValid());

    const QUrlQuery query(url);
    QCOMPARE(formValue(query, "client_id"), kClient.clientId);
    QCOMPARE(formValue(query, "response_type"), QStringLiteral("code"));
    // Newer Qt passes scopes as a set, so only membership is fixed, not order.
    const QStringList requested = formValue(query, "scope").split(u' ');
    const QStringList expected = GoogleAuth::scopes().split(u' ');
    QCOMPARE(QSet<QString>(requested.begin(), requested.end()),
             QSet<QString>(expected.begin(), expected.end()));
    QCOMPARE(formValue(query, "code_challenge_method"), QStringLiteral("S256"));
    QCOMPARE(formValue(query, "code_challenge").size(), 43);
    QVERIFY(!formValue(query, "state").isEmpty());
    QCOMPARE(formValue(query, "access_type"), QStringLiteral("offline"));
    QCOMPARE(formValue(query, "prompt"), QStringLiteral("consent"));
    QCOMPARE(QUrl(formValue(query, "redirect_uri")), auth.callbackUrl());
    QVERIFY(!query.hasQueryItem(QStringLiteral("client_secret")));
}

void TestGoogleAuth::callbackListensOnLoopbackOnly()
{
    FakeHttpServer tokenServer;
    GoogleAuth auth(kClient);
    QVERIFY(GoogleAuthDriver::startAuthorization(auth, tokenServer).isValid());

    const QUrl callback = auth.callbackUrl();
    QCOMPARE(callback.host(), QStringLiteral("127.0.0.1"));
    QVERIFY(callback.port() > 0);

    QTcpSocket loopback;
    loopback.connectToHost(QHostAddress::LocalHost, quint16(callback.port()));
    QVERIFY(loopback.waitForConnected(2000));

    QHostAddress external;
    for (const QHostAddress &address : QNetworkInterface::allAddresses()) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback()) {
            external = address;
            break;
        }
    }
    if (external.isNull())
        QSKIP("no non-loopback IPv4 address to probe");

    QTcpSocket remote;
    remote.connectToHost(external, quint16(callback.port()));
    QVERIFY2(!remote.waitForConnected(2000),
             "callback port is reachable off the loopback interface");
}

void TestGoogleAuth::codeExchangeSendsVerifierMatchingChallenge()
{
    FakeHttpServer tokenServer;
    tokenServer.respond(200, R"({"access_token":"at-1","refresh_token":"rt-1",)"
                             R"("expires_in":3600,"token_type":"Bearer"})");
    GoogleAuth auth(kClient);
    QSignalSpy granted(&auth, &GoogleAuth::granted);
    QNetworkAccessManager network;

    const QUrlQuery authQuery(GoogleAuthDriver::startAuthorization(auth, tokenServer));
    GoogleAuthDriver::redirectBack(network, auth, formValue(authQuery, "state"));

    QVERIFY(granted.wait(5000));
    const auto tokens = granted.first().first().value<GoogleTokens>();
    QCOMPARE(tokens.accessToken, QStringLiteral("at-1"));
    QCOMPARE(tokens.refreshToken, QStringLiteral("rt-1"));

    QCOMPARE(tokenServer.requests.size(), 1);
    const QUrlQuery body = formBody(tokenServer.requests.first().body);
    QCOMPARE(formValue(body, "grant_type"), QStringLiteral("authorization_code"));
    QCOMPARE(formValue(body, "code"), QStringLiteral("the-code"));
    QCOMPARE(QUrl(formValue(body, "redirect_uri")), auth.callbackUrl());

    // RFC 7636: the challenge is the unpadded base64url SHA-256 of the verifier.
    const QByteArray verifier = formValue(body, "code_verifier").toLatin1();
    QVERIFY(!verifier.isEmpty());
    const QByteArray challenge =
        QCryptographicHash::hash(verifier, QCryptographicHash::Sha256)
            .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    QCOMPARE(QString::fromLatin1(challenge), formValue(authQuery, "code_challenge"));
}

void TestGoogleAuth::mismatchedStateIsNotExchanged()
{
    FakeHttpServer tokenServer;
    GoogleAuth auth(kClient);
    QSignalSpy granted(&auth, &GoogleAuth::granted);
    QNetworkAccessManager network;

    QVERIFY(GoogleAuthDriver::startAuthorization(auth, tokenServer).isValid());
    GoogleAuthDriver::redirectBack(network, auth, QStringLiteral("forged-state"));

    QTest::qWait(1000);
    QCOMPARE(tokenServer.requests.size(), 0);
    QCOMPARE(granted.size(), 0);
}

void TestGoogleAuth::missingRefreshTokenFails()
{
    FakeHttpServer tokenServer;
    tokenServer.respond(200, R"({"access_token":"at-1","expires_in":3600,"token_type":"Bearer"})");
    GoogleAuth auth(kClient);
    QSignalSpy granted(&auth, &GoogleAuth::granted);
    QSignalSpy failed(&auth, &GoogleAuth::failed);
    QNetworkAccessManager network;

    const QUrlQuery authQuery(GoogleAuthDriver::startAuthorization(auth, tokenServer));
    GoogleAuthDriver::redirectBack(network, auth, formValue(authQuery, "state"));

    QVERIFY(failed.wait(5000));
    QCOMPARE(granted.size(), 0);
}

void TestGoogleAuth::refreshKeepsExistingRefreshToken()
{
    FakeHttpServer tokenServer;
    tokenServer.respond(200, R"({"access_token":"at-2","expires_in":3600,"token_type":"Bearer"})");
    GoogleAuth auth(kClient);
    GoogleAuthDriver::useTokenServer(auth, tokenServer);
    QSignalSpy granted(&auth, &GoogleAuth::granted);

    auth.refresh(QStringLiteral("rt-1"));

    QVERIFY(granted.wait(5000));
    const auto tokens = granted.first().first().value<GoogleTokens>();
    QCOMPARE(tokens.accessToken, QStringLiteral("at-2"));
    QCOMPARE(tokens.refreshToken, QStringLiteral("rt-1"));

    QCOMPARE(tokenServer.requests.size(), 1);
    const QUrlQuery body = formBody(tokenServer.requests.first().body);
    QCOMPARE(formValue(body, "grant_type"), QStringLiteral("refresh_token"));
    QCOMPARE(formValue(body, "refresh_token"), QStringLiteral("rt-1"));
}

void TestGoogleAuth::primaryCalendarIdSendsBearerToken()
{
    FakeHttpServer server;
    server.respond(200, R"({"id":"me@example.com"})");
    QNetworkAccessManager network;
    GoogleCalendarApi api(&network);
    api.setBaseUrl(server.url(QStringLiteral("/calendar/v3/")));

    QString id, error;
    bool done = false;
    api.fetchPrimaryCalendarId(QStringLiteral("at-1"), [&](const QString &i, const QString &e) {
        id = i;
        error = e;
        done = true;
    });

    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QCOMPARE(error, QString());
    QCOMPARE(id, QStringLiteral("me@example.com"));
    QCOMPARE(server.requests.first().target,
             QByteArray("/calendar/v3/users/me/calendarList/primary"));
    QCOMPARE(server.requests.first().headers.value("authorization"), QByteArray("Bearer at-1"));
}

void TestGoogleAuth::apiErrorMessageIsReported()
{
    FakeHttpServer server;
    server.respond(401, R"({"error":{"code":401,"message":"Invalid Credentials"}})");
    QNetworkAccessManager network;
    GoogleCalendarApi api(&network);
    api.setBaseUrl(server.url(QStringLiteral("/calendar/v3/")));

    QString error;
    bool done = false;
    api.fetchPrimaryCalendarId(QStringLiteral("expired"), [&](const QString &, const QString &e) {
        error = e;
        done = true;
    });

    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QCOMPARE(error, QStringLiteral("Invalid Credentials"));
}

QTEST_GUILESS_MAIN(TestGoogleAuth)
#include "tst_googleauth.moc"
