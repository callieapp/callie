#include "GoogleAuthDriver.h"

#include "callie/GoogleAuth.h"
#include "callie/GoogleClientConfig.h"
#include "callie/ThemeLoader.h"

#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTest>
#include <QUrlQuery>

using namespace callie;
using namespace Qt::StringLiterals;

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
    void signingInAgainPicksTheAccount();
    void callbackListensOnLoopbackOnly();
    void callbackPageIsThemed();
    void codeExchangeSendsVerifierMatchingChallenge();
    void mismatchedStateIsNotExchanged();
    void missingRefreshTokenFails();
    void refreshKeepsExistingRefreshToken();
    void refreshSendsClientAndReportsExpiry();
    void revokedRefreshSaysToReconnect();
    void refreshErrorCarriesGoogleReason();
    void refreshWithoutErrorBodyStillFails();
    void refreshWithoutAccessTokenFails();
    void authorizeAgainAfterFailure();
    void signInTimesOutWithoutAnswer();
    void timeoutStopsOnceGoogleAnswers();
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
    // A new account picks its own address.
    QVERIFY(!query.hasQueryItem(QStringLiteral("login_hint")));
}

void TestGoogleAuth::signingInAgainPicksTheAccount()
{
    FakeHttpServer tokenServer;
    GoogleAuth auth(kClient);
    auth.setLoginHint(QStringLiteral("me@example.com"));
    const QUrlQuery query(GoogleAuthDriver::startAuthorization(auth, tokenServer));
    QCOMPARE(formValue(query, "login_hint"), QStringLiteral("me@example.com"));
    // Still asks for consent, so permissions added since are granted too.
    QCOMPARE(formValue(query, "prompt"), QStringLiteral("consent"));
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

void TestGoogleAuth::callbackPageIsThemed()
{
    FakeHttpServer tokenServer;
    GoogleAuth auth(kClient);
    const QUrl authorizeUrl = GoogleAuthDriver::startAuthorization(auth, tokenServer);
    QVERIFY(authorizeUrl.isValid());

    QUrl callback = auth.callbackUrl();
    callback.setQuery(u"code=c&state="_s +
                      QUrlQuery(authorizeUrl).queryItemValue(u"state"_s, QUrl::FullyEncoded));
    QNetworkAccessManager network;
    std::unique_ptr<QNetworkReply> reply(network.get(QNetworkRequest(callback)));
    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 5000);
    const QString page = QString::fromUtf8(reply->readAll());

    // Markup must reach the browser as markup, in the default theme's colors.
    QVERIFY2(page.contains(u"<style>"_s), qPrintable(page.left(200)));
    QVERIFY(page.contains(ThemeLoader::defaultTheme().colors.background.name()));
    QVERIFY(page.contains(u"<svg"_s));
    QVERIFY(page.contains(u"All done here"_s));
    QVERIFY(
        page.contains(u"font-family: 'Fraunces'; font-weight: 100 900; src: url(data:font/ttf"_s));
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

void TestGoogleAuth::refreshSendsClientAndReportsExpiry()
{
    FakeHttpServer tokenServer;
    tokenServer.respond(200, R"({"access_token":"at-2","expires_in":3600,"token_type":"Bearer"})");
    GoogleAuth auth(kClient);
    GoogleAuthDriver::useTokenServer(auth, tokenServer);
    QSignalSpy granted(&auth, &GoogleAuth::granted);

    const QDateTime before = QDateTime::currentDateTimeUtc();
    auth.refresh(QStringLiteral("rt-1"));

    QVERIFY(granted.wait(5000));
    const auto tokens = granted.first().first().value<GoogleTokens>();
    QVERIFY(tokens.expiresAt >= before.addSecs(3600));
    QVERIFY(tokens.expiresAt <= QDateTime::currentDateTimeUtc().addSecs(3600));
    const QUrlQuery body = formBody(tokenServer.requests.first().body);
    QCOMPARE(formValue(body, "client_id"), kClient.clientId);
    QCOMPARE(formValue(body, "client_secret"), kClient.clientSecret);
    QCOMPARE(tokenServer.requests.first().method, QByteArray("POST"));
}

void TestGoogleAuth::revokedRefreshSaysToReconnect()
{
    FakeHttpServer tokenServer;
    tokenServer.respond(400, R"({"error":"invalid_grant",
                                 "error_description":"Token has been expired or revoked."})");
    GoogleAuth auth(kClient);
    GoogleAuthDriver::useTokenServer(auth, tokenServer);
    QSignalSpy failed(&auth, &GoogleAuth::failed);
    QSignalSpy granted(&auth, &GoogleAuth::granted);

    auth.refresh(QStringLiteral("rt-1"));

    QVERIFY(failed.wait(5000));
    const QString message = failed.first().first().toString();
    QVERIFY2(message.contains(u"Token has been expired or revoked."), qPrintable(message));
    QVERIFY2(message.contains(u"Connect the account again"), qPrintable(message));
    QCOMPARE(granted.size(), 0);
}

void TestGoogleAuth::refreshErrorCarriesGoogleReason()
{
    FakeHttpServer tokenServer;
    tokenServer.respond(401, R"({"error":"invalid_client",
                                 "error_description":"The OAuth client was not found."})");
    GoogleAuth auth(kClient);
    GoogleAuthDriver::useTokenServer(auth, tokenServer);
    QSignalSpy failed(&auth, &GoogleAuth::failed);

    auth.refresh(QStringLiteral("rt-1"));

    QVERIFY(failed.wait(5000));
    const QString message = failed.first().first().toString();
    QVERIFY2(message.contains(u"The OAuth client was not found."), qPrintable(message));
    QVERIFY(!message.contains(u"Connect the account again"));
}

void TestGoogleAuth::refreshWithoutErrorBodyStillFails()
{
    FakeHttpServer tokenServer;
    tokenServer.respond(503, "unavailable");
    GoogleAuth auth(kClient);
    GoogleAuthDriver::useTokenServer(auth, tokenServer);
    QSignalSpy failed(&auth, &GoogleAuth::failed);

    auth.refresh(QStringLiteral("rt-1"));

    QVERIFY(failed.wait(5000));
    const QString message = failed.first().first().toString();
    QVERIFY2(message.startsWith(u"could not refresh the Google sign-in: "), qPrintable(message));
}

void TestGoogleAuth::refreshWithoutAccessTokenFails()
{
    FakeHttpServer tokenServer;
    tokenServer.respond(200, R"({"token_type":"Bearer"})");
    GoogleAuth auth(kClient);
    GoogleAuthDriver::useTokenServer(auth, tokenServer);
    QSignalSpy failed(&auth, &GoogleAuth::failed);

    auth.refresh(QStringLiteral("rt-1"));

    QVERIFY(failed.wait(5000));
    QCOMPARE(failed.first().first().toString(), QStringLiteral("Google returned no access token"));
}

void TestGoogleAuth::authorizeAgainAfterFailure()
{
    // The first attempt fails at the token exchange, which closes the listener.
    FakeHttpServer tokenServer;
    tokenServer.respond(400, R"({"error":"invalid_grant","error_description":"Bad Request"})");
    GoogleAuth auth(kClient);
    QSignalSpy failed(&auth, &GoogleAuth::failed);
    QSignalSpy granted(&auth, &GoogleAuth::granted);
    QNetworkAccessManager network;

    const QUrlQuery first(GoogleAuthDriver::startAuthorization(auth, tokenServer));
    GoogleAuthDriver::redirectBack(network, auth, formValue(first, "state"));
    QVERIFY(failed.wait(5000));

    // The retry must listen again, on loopback, and complete normally.
    tokenServer.respond(200, R"({"access_token":"at-2","refresh_token":"rt-2",)"
                             R"("expires_in":3600,"token_type":"Bearer"})");
    QSignalSpy ready(&auth, &GoogleAuth::authorizeUrlReady);
    auth.authorize();
    QVERIFY(!ready.isEmpty() || ready.wait(2000));
    QCOMPARE(auth.callbackUrl().host(), QStringLiteral("127.0.0.1"));

    const QUrlQuery second(ready.first().first().toUrl());
    GoogleAuthDriver::redirectBack(network, auth, formValue(second, "state"));
    QVERIFY(granted.wait(5000));
    QCOMPARE(granted.first().first().value<GoogleTokens>().refreshToken, QStringLiteral("rt-2"));
}

void TestGoogleAuth::signInTimesOutWithoutAnswer()
{
    FakeHttpServer tokenServer;
    GoogleAuth auth(kClient);
    auth.setSignInTimeout(std::chrono::milliseconds(200));
    QSignalSpy failed(&auth, &GoogleAuth::failed);

    QVERIFY(GoogleAuthDriver::startAuthorization(auth, tokenServer).isValid());

    QVERIFY(failed.wait(2000));
    QVERIFY(failed.first().first().toString().contains(QStringLiteral("timed out")));
}

void TestGoogleAuth::timeoutStopsOnceGoogleAnswers()
{
    // Whatever runs after the grant, such as a slow keyring unlock, must not be
    // cut off by the sign-in timeout.
    FakeHttpServer tokenServer;
    tokenServer.respond(200, R"({"access_token":"at-1","refresh_token":"rt-1",)"
                             R"("expires_in":3600,"token_type":"Bearer"})");
    GoogleAuth auth(kClient);
    auth.setSignInTimeout(std::chrono::milliseconds(400));
    QSignalSpy granted(&auth, &GoogleAuth::granted);
    QSignalSpy failed(&auth, &GoogleAuth::failed);
    QNetworkAccessManager network;

    const QUrlQuery query(GoogleAuthDriver::startAuthorization(auth, tokenServer));
    GoogleAuthDriver::redirectBack(network, auth, formValue(query, "state"));
    QVERIFY(granted.wait(2000));

    QTest::qWait(800);
    QCOMPARE(failed.size(), 0);
}

QTEST_GUILESS_MAIN(TestGoogleAuth)
#include "tst_googleauth.moc"
