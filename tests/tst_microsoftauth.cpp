#include "AuthDriver.h"
#include "FakeTokenStore.h"

#include "callie/AccountManager.h"
#include "callie/AccountStore.h"
#include "callie/GraphApi.h"
#include "callie/MicrosoftAuth.h"
#include "callie/MicrosoftTokenProvider.h"

#include <QNetworkAccessManager>
#include <QSignalSpy>
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

QTEST_GUILESS_MAIN(TestMicrosoftAuth)
#include "tst_microsoftauth.moc"
