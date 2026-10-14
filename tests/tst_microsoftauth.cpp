#include "AuthDriver.h"
#include "FakeTokenStore.h"

#include "callie/AccountManager.h"
#include "callie/AccountStore.h"
#include "callie/GraphApi.h"
#include "callie/MicrosoftAuth.h"
#include "callie/MicrosoftTokenProvider.h"

#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

const MicrosoftClientConfig kClient{u"app-id"_s};
const Account kAccount{u"microsoft"_s, u"me@outlook.com"_s};

QString formValue(const QByteArray &body, const char *key)
{
    return QUrlQuery(QString::fromUtf8(body))
        .queryItemValue(QString::fromLatin1(key), QUrl::FullyDecoded);
}

} // namespace

class TestMicrosoftAuth : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void environmentReplacesBuiltInApp();
    void authorizationAsksForCalendarsAndARefreshToken();
    void codeExchangeSendsNoSecret();
    void refreshStoresTheNewRefreshToken();
    void revokedRefreshSaysToReconnect();
    void connectingListsTheAccountByAddress();
    void callbackListensOnLoopbackOnly();
    void mismatchedStateIsNotExchanged();
    void failedSignInsStoreNothing_data();
    void failedSignInsStoreNothing();
    void anUnchangedRefreshTokenIsNotWrittenAgain();
    void aKeyringThatWillNotWriteStillGivesTheToken();
    void requestsTogetherShareOneRefresh();
    void anUnreadableKeyringSaysSo();
    void graphFollowsPagesOnItsOwnHostOnly();
    void graphErrorsSayWhatMicrosoftSaid();
};

void TestMicrosoftAuth::environmentReplacesBuiltInApp()
{
    QProcessEnvironment environment;
    QCOMPARE(MicrosoftClientConfig::resolve({u"built-in"_s}, environment).clientId, u"built-in"_s);
    environment.insert(u"CALLIE_MICROSOFT_CLIENT_ID"_s, u"from-env"_s);
    QCOMPARE(MicrosoftClientConfig::resolve({u"built-in"_s}, environment).clientId, u"from-env"_s);
}

void TestMicrosoftAuth::authorizationAsksForCalendarsAndARefreshToken()
{
    FakeHttpServer tokenServer;
    MicrosoftAuth auth(kClient);
    auth.setLoginHint(u"me@outlook.com"_s);
    const QUrlQuery query(AuthDriver::startAuthorization(auth, tokenServer));
    const QStringList scopes = query.queryItemValue(u"scope"_s, QUrl::FullyDecoded).split(u' ');
    for (const QString &scope :
         {u"offline_access"_s, u"Calendars.ReadWrite"_s, u"User.Read"_s, u"People.Read"_s})
        QVERIFY2(scopes.contains(scope), qPrintable(scope));
    QCOMPARE(query.queryItemValue(u"client_id"_s), u"app-id"_s);
    QCOMPARE(query.queryItemValue(u"code_challenge_method"_s), u"S256"_s);
    QCOMPARE(query.queryItemValue(u"prompt"_s), u"select_account"_s);
    QCOMPARE(query.queryItemValue(u"login_hint"_s, QUrl::FullyDecoded), u"me@outlook.com"_s);
    QCOMPARE(auth.callbackUrl().host(), u"127.0.0.1"_s);
}

void TestMicrosoftAuth::codeExchangeSendsNoSecret()
{
    FakeHttpServer tokenServer;
    tokenServer.respond(200, R"({"access_token":"at-1","refresh_token":"rt-1",)"
                             R"("expires_in":3600,"token_type":"Bearer"})");
    MicrosoftAuth auth(kClient);
    QSignalSpy granted(&auth, &MicrosoftAuth::granted);
    QNetworkAccessManager network;
    const QUrlQuery query(AuthDriver::startAuthorization(auth, tokenServer));
    AuthDriver::redirectBack(network, auth, query.queryItemValue(u"state"_s, QUrl::FullyDecoded));
    QVERIFY(granted.wait(5000));
    QCOMPARE(granted.first().first().value<MicrosoftTokens>().refreshToken, u"rt-1"_s);
    const QByteArray body = tokenServer.requests.first().body;
    QVERIFY(!formValue(body, "code_verifier").isEmpty());
    QVERIFY(!body.contains("client_secret"));
}

void TestMicrosoftAuth::refreshStoresTheNewRefreshToken()
{
    FakeHttpServer tokenServer;
    tokenServer.respond(200, R"({"access_token":"at-2","refresh_token":"rt-2",)"
                             R"("expires_in":3600})");
    FakeTokenStore tokens;
    tokens.secrets.insert(kAccount.id, u"rt-1"_s);
    MicrosoftTokenProvider provider(kClient, tokens);
    provider.setTokenUrl(tokenServer.url(u"/token"_s));
    QString access = u"no answer"_s;
    provider.accessToken(kAccount,
                         [&access](const QString &token, const QString &) { access = token; });
    QTRY_COMPARE(access, u"at-2"_s);
    // The spent one is replaced, and the scope is asked again, as Microsoft wants.
    QCOMPARE(tokens.secrets.value(kAccount.id), u"rt-2"_s);
    const QByteArray body = tokenServer.requests.first().body;
    QCOMPARE(formValue(body, "refresh_token"), u"rt-1"_s);
    QVERIFY(formValue(body, "scope").contains(u"Calendars.ReadWrite"_s));

    // Cached after that, until it expires.
    access.clear();
    provider.accessToken(kAccount,
                         [&access](const QString &token, const QString &) { access = token; });
    QTRY_COMPARE(access, u"at-2"_s);
    QCOMPARE(tokenServer.requests.size(), 1);
}

void TestMicrosoftAuth::revokedRefreshSaysToReconnect()
{
    FakeHttpServer tokenServer;
    tokenServer.respond(400,
                        R"({"error":"invalid_grant",)"
                        R"("error_description":"AADSTS70008: The refresh token has expired."})");
    FakeTokenStore tokens;
    tokens.secrets.insert(kAccount.id, u"rt-1"_s);
    MicrosoftTokenProvider provider(kClient, tokens);
    provider.setTokenUrl(tokenServer.url(u"/token"_s));
    QString error;
    provider.accessToken(kAccount,
                         [&error](const QString &, const QString &failed) { error = failed; });
    QTRY_VERIFY(!error.isEmpty());
    QVERIFY(error.contains(u"Connect the account again"_s));
    QVERIFY(error.contains(u"AADSTS70008"_s));
}

void TestMicrosoftAuth::connectingListsTheAccountByAddress()
{
    QTemporaryDir dir;
    AccountStore store(dir.filePath(u"accounts.json"_s));
    FakeTokenStore tokens;
    AccountManager manager(tokens, store);
    FakeHttpServer tokenServer;
    tokenServer.respond(200, R"({"access_token":"at-1","refresh_token":"rt-1","expires_in":3600})");
    FakeHttpServer graphServer;
    // A personal account, with no mail but its sign-in name.
    graphServer.respond(200, R"({"id":"abc","mail":null,"userPrincipalName":"Me@Outlook.com",)"
                             R"("displayName":"Me"})");
    QNetworkAccessManager network;
    MicrosoftAuth auth(kClient);
    AuthDriver::useTokenServer(auth, tokenServer);
    GraphApi graph(&network);
    graph.setBaseUrl(graphServer.url(u"/v1.0/"_s));
    QSignalSpy connected(&manager, &AccountManager::connected);
    AuthDriver::approveNextAuthorization(network, auth);
    manager.connectMicrosoft(auth, graph);
    QVERIFY(connected.wait(5000));
    QCOMPARE(connected.first().first().value<Account>(), kAccount);
    QCOMPARE(tokens.secrets.value(kAccount.id), u"rt-1"_s);
    QCOMPARE(graphServer.requests.first().headers.value("authorization"), "Bearer at-1");
    QList<Account> listed;
    QVERIFY(store.load(listed));
    QCOMPARE(listed, QList<Account>{kAccount});
}

void TestMicrosoftAuth::callbackListensOnLoopbackOnly()
{
    FakeHttpServer tokenServer;
    MicrosoftAuth auth(kClient);
    QVERIFY(AuthDriver::startAuthorization(auth, tokenServer).isValid());
    const QUrl callback = auth.callbackUrl();
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

void TestMicrosoftAuth::mismatchedStateIsNotExchanged()
{
    FakeHttpServer tokenServer;
    MicrosoftAuth auth(kClient);
    QSignalSpy granted(&auth, &MicrosoftAuth::granted);
    QNetworkAccessManager network;
    QVERIFY(AuthDriver::startAuthorization(auth, tokenServer).isValid());
    AuthDriver::redirectBack(network, auth, u"forged-state"_s);
    QVERIFY(!granted.wait(500));
    QVERIFY(tokenServer.requests.isEmpty());
}

void TestMicrosoftAuth::failedSignInsStoreNothing_data()
{
    QTest::addColumn<int>("tokenStatus");
    QTest::addColumn<QByteArray>("tokenBody");
    QTest::addColumn<int>("graphStatus");
    QTest::addColumn<QByteArray>("graphBody");
    const QByteArray tokens = R"({"access_token":"at-1","refresh_token":"rt-1","expires_in":3600})";
    QTest::newRow("sign-in refused")
        << 400 << QByteArray(R"({"error":"invalid_grant"})") << 200 << QByteArray("{}");
    QTest::newRow("no refresh token")
        << 200 << QByteArray(R"({"access_token":"at-1","expires_in":3600})") << 200
        << QByteArray(R"({"mail":"me@outlook.com"})");
    QTest::newRow("graph refuses")
        << 200 << tokens << 403 << QByteArray(R"({"error":{"message":"Forbidden"}})");
    QTest::newRow("no address") << 200 << tokens << 200 << QByteArray(R"({"id":"abc"})");
}

void TestMicrosoftAuth::failedSignInsStoreNothing()
{
    QFETCH(int, tokenStatus);
    QFETCH(QByteArray, tokenBody);
    QFETCH(int, graphStatus);
    QFETCH(QByteArray, graphBody);
    QTemporaryDir dir;
    AccountStore store(dir.filePath(u"accounts.json"_s));
    FakeTokenStore tokens;
    AccountManager manager(tokens, store);
    FakeHttpServer tokenServer;
    tokenServer.respond(tokenStatus, tokenBody);
    FakeHttpServer graphServer;
    graphServer.respond(graphStatus, graphBody);
    QNetworkAccessManager network;
    MicrosoftAuth auth(kClient);
    AuthDriver::useTokenServer(auth, tokenServer);
    GraphApi graph(&network);
    graph.setBaseUrl(graphServer.url(u"/v1.0/"_s));
    QSignalSpy connected(&manager, &AccountManager::connected);
    QSignalSpy failed(&manager, &AccountManager::failed);
    AuthDriver::approveNextAuthorization(network, auth);
    manager.connectMicrosoft(auth, graph);
    QVERIFY(failed.wait(5000));
    QTest::qWait(100);
    QCOMPARE(connected.size(), 0);
    QCOMPARE(failed.size(), 1);
    QVERIFY(tokens.secrets.isEmpty());
    QList<Account> listed;
    QVERIFY(store.load(listed));
    QVERIFY(listed.isEmpty());
}

void TestMicrosoftAuth::anUnchangedRefreshTokenIsNotWrittenAgain()
{
    FakeHttpServer tokenServer;
    tokenServer.respond(200, R"({"access_token":"at-2","refresh_token":"rt-1","expires_in":3600})");
    FakeTokenStore tokens;
    tokens.secrets.insert(kAccount.id, u"rt-1"_s);
    MicrosoftTokenProvider provider(kClient, tokens);
    provider.setTokenUrl(tokenServer.url(u"/token"_s));
    QString access;
    provider.accessToken(kAccount,
                         [&access](const QString &token, const QString &) { access = token; });
    QTRY_COMPARE(access, u"at-2"_s);
    QCOMPARE(tokens.writes, 0);
}

void TestMicrosoftAuth::aKeyringThatWillNotWriteStillGivesTheToken()
{
    FakeHttpServer tokenServer;
    tokenServer.respond(200, R"({"access_token":"at-2","refresh_token":"rt-2","expires_in":3600})");
    FakeTokenStore tokens;
    tokens.secrets.insert(kAccount.id, u"rt-1"_s);
    tokens.writeFailure = u"keyring is locked"_s;
    MicrosoftTokenProvider provider(kClient, tokens);
    provider.setTokenUrl(tokenServer.url(u"/token"_s));
    QString access;
    QString error = u"no answer"_s;
    provider.accessToken(kAccount, [&](const QString &token, const QString &failed) {
        access = token;
        error = failed;
    });
    QTRY_COMPARE(access, u"at-2"_s);
    QVERIFY(error.isEmpty());
    QCOMPARE(tokens.writes, 1);
}

void TestMicrosoftAuth::requestsTogetherShareOneRefresh()
{
    FakeHttpServer tokenServer;
    tokenServer.respond(200, R"({"access_token":"at-2","refresh_token":"rt-2","expires_in":3600})");
    FakeTokenStore tokens;
    tokens.secrets.insert(kAccount.id, u"rt-1"_s);
    MicrosoftTokenProvider provider(kClient, tokens);
    provider.setTokenUrl(tokenServer.url(u"/token"_s));
    QStringList answers;
    for (int i = 0; i < 3; ++i)
        provider.accessToken(
            kAccount, [&answers](const QString &token, const QString &) { answers << token; });
    QTRY_COMPARE(answers.size(), 3);
    QCOMPARE(answers, QStringList(3, u"at-2"_s));
    // One refresh spends one refresh token, as Microsoft allows.
    QCOMPARE(tokenServer.requests.size(), 1);
}

void TestMicrosoftAuth::anUnreadableKeyringSaysSo()
{
    FakeTokenStore tokens;
    tokens.failWith = u"no keyring"_s;
    MicrosoftTokenProvider provider(kClient, tokens);
    QString error;
    provider.accessToken(kAccount,
                         [&error](const QString &, const QString &failed) { error = failed; });
    QTRY_VERIFY(!error.isEmpty());
    QVERIFY(error.contains(u"no keyring"_s));
}

void TestMicrosoftAuth::graphFollowsPagesOnItsOwnHostOnly()
{
    FakeHttpServer server;
    const QByteArray next = server.url(u"/v1.0/me/events?page=2"_s).toEncoded();
    server.enqueue(200, R"({"value":[{"id":"a"}],"@odata.nextLink":")" + next + R"("})");
    server.enqueue(200, R"({"value":[{"id":"b"}]})");
    QNetworkAccessManager network;
    GraphApi graph(&network);
    graph.setBaseUrl(server.url(u"/v1.0/"_s));
    QList<QJsonObject> items;
    bool done = false;
    graph.list(u"at"_s, u"me/events"_s, [&](const QList<QJsonObject> &found, const GraphError &) {
        items = found;
        done = true;
    });
    QTRY_VERIFY(done);
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.last()[u"id"].toString(), u"b"_s);
    QCOMPARE(server.requests.last().headers.value("authorization"), "Bearer at");

    // A next page elsewhere would take the token with it, so it is not followed.
    server.enqueue(200, R"({"value":[{"id":"a"}],"@odata.nextLink":"https://example.com/steal"})");
    GraphError error;
    done = false;
    graph.list(u"at"_s, u"me/events"_s, [&](const QList<QJsonObject> &, const GraphError &failed) {
        error = failed;
        done = true;
    });
    QTRY_VERIFY(done);
    QVERIFY(error);
}

void TestMicrosoftAuth::graphErrorsSayWhatMicrosoftSaid()
{
    FakeHttpServer server;
    server.enqueue(429, R"({"error":{"code":"TooManyRequests","message":"Slow down"}})");
    server.enqueue(401, R"({"error":{"code":"InvalidAuthenticationToken","message":"Expired"}})");
    QNetworkAccessManager network;
    GraphApi graph(&network);
    graph.setBaseUrl(server.url(u"/v1.0/"_s));
    QList<GraphError> errors;
    for (int i = 0; i < 2; ++i)
        graph.send(u"at"_s, "GET", u"me"_s, {},
                   [&errors](const QJsonObject &, const GraphError &e) { errors << e; });
    QTRY_COMPARE(errors.size(), 2);
    QCOMPARE(errors.first().message, u"Slow down"_s);
    QVERIFY(errors.first().later());
    QVERIFY(errors.last().unauthorized());
    QVERIFY(!errors.last().later());
}

QTEST_GUILESS_MAIN(TestMicrosoftAuth)
#include "tst_microsoftauth.moc"
