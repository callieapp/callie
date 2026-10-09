#include "AccountsController.h"
#include "FakeHttpServer.h"
#include "FakeTokenStore.h"

#include "callie/AccountStore.h"
#include "callie/GoogleCache.h"
#include "callie/GoogleSource.h"

#include <QDesktopServices>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

/// Stands in for the user's browser: it consents at once by following the
/// sign-in page's redirect back to Callie.
class Browser : public QObject
{
    Q_OBJECT

public:
    QNetworkAccessManager network;
    int opened = 0;
    QUrl last;

public Q_SLOTS:
    void open(const QUrl &url)
    {
        ++opened;
        last = url;
        const QUrlQuery query(url);
        QUrl callback(query.queryItemValue(u"redirect_uri"_s, QUrl::FullyDecoded));
        QUrlQuery answer;
        answer.addQueryItem(u"code"_s, u"the-code"_s);
        answer.addQueryItem(u"state"_s, query.queryItemValue(u"state"_s, QUrl::FullyDecoded));
        callback.setQuery(answer);
        QNetworkReply *reply = network.get(QNetworkRequest(callback));
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    }
};

const Account kMe{u"google"_s, u"me@example.com"_s};

} // namespace

class TestAccounts : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void listsAndRemovesAccounts();
    void sampleModeTouchesNothing();
    void signingInAgainSyncsAndHoldsOffOtherActions();
    void failedSignInEndsTheAttempt();
    void reconnectPicksTheAccount();

private:
    /// Signs in through fake Google endpoints answering with these responses,
    /// or signs `again` in again.
    void signIn(int tokenStatus, const QByteArray &tokenBody, const QString &again = {});

    Browser m_browser;
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<AccountStore> m_store;
    std::unique_ptr<FakeTokenStore> m_tokens;
    std::unique_ptr<GoogleCache> m_cache;
    std::unique_ptr<GoogleSource> m_source;
    std::unique_ptr<FakeHttpServer> m_tokenServer;
    std::unique_ptr<FakeHttpServer> m_apiServer;
};

void TestAccounts::listsAndRemovesAccounts()
{
    QTemporaryDir dir;
    AccountStore store(dir.filePath(u"accounts.json"_s));
    const Account me{u"google"_s, u"me@example.com"_s};
    const Account other{u"google"_s, u"other@example.com"_s};
    QVERIFY(store.add(me));
    QVERIFY(store.add(other));
    FakeTokenStore tokens;
    GoogleCache cache(dir.filePath(u"google.sqlite"_s));
    QVERIFY(cache.open());
    GoogleSource source(cache, {});
    QNetworkAccessManager network;

    AccountsController *accounts = AccountsController::instance();
    accounts->setUp(
        {&store, &tokens, &cache, &source, &network, GoogleClientConfig{u"id"_s, u"secret"_s}, {}});

    QCOMPARE(accounts->accounts().size(), 2);
    QVERIFY(accounts->unavailable().isEmpty());
    // The source follows the stored list.
    QCOMPARE(source.accounts(), (QList<Account>{me, other}));

    accounts->remove(me.id);
    QVERIFY(!accounts->signingIn());
    QTRY_VERIFY_WITH_TIMEOUT(!accounts->busy(), 5000);
    QVERIFY2(accounts->error().isEmpty(), qPrintable(accounts->error()));
    QCOMPARE(accounts->accounts().size(), 1);
    QCOMPARE(source.accounts(), QList<Account>{other});
}

void TestAccounts::sampleModeTouchesNothing()
{
    AccountsController *accounts = AccountsController::instance();
    AccountsController::Setup setup;
    setup.unavailable = u"Showing sample data"_s;
    accounts->setUp(setup);

    QVERIFY(accounts->accounts().isEmpty());
    QCOMPARE(accounts->unavailable(), u"Showing sample data"_s);
    accounts->connectGoogle();
    QVERIFY(!accounts->busy());
}

void TestAccounts::signIn(int tokenStatus, const QByteArray &tokenBody, const QString &again)
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_store = std::make_unique<AccountStore>(m_dir->filePath(u"accounts.json"_s));
    QVERIFY(m_store->add(kMe));
    m_tokens = std::make_unique<FakeTokenStore>();
    m_cache = std::make_unique<GoogleCache>(m_dir->filePath(u"google.sqlite"_s));
    QVERIFY(m_cache->open());
    m_source = std::make_unique<GoogleSource>(*m_cache, QList<Account>{});
    m_tokenServer = std::make_unique<FakeHttpServer>();
    m_tokenServer->respond(tokenStatus, tokenBody);
    m_apiServer = std::make_unique<FakeHttpServer>();
    m_apiServer->respond(200, R"({"id":"me@example.com"})");
    QDesktopServices::setUrlHandler(u"https"_s, &m_browser, "open");

    AccountsController::Setup setup{m_store.get(),
                                    m_tokens.get(),
                                    m_cache.get(),
                                    m_source.get(),
                                    &m_browser.network,
                                    GoogleClientConfig{u"id"_s, u"secret"_s},
                                    {}};
    setup.authUrl = QUrl(u"https://accounts.example/auth"_s);
    setup.tokenUrl = m_tokenServer->url(u"/token"_s);
    setup.apiBaseUrl = m_apiServer->url(u"/calendar/v3/"_s);
    AccountsController::instance()->setUp(setup);
    if (again.isEmpty())
        AccountsController::instance()->connectGoogle();
    else
        AccountsController::instance()->reconnect(again);
}

void TestAccounts::reconnectPicksTheAccount()
{
    AccountsController *accounts = AccountsController::instance();
    signIn(200, R"({"access_token":"at-2","refresh_token":"rt-2","expires_in":3600})", kMe.id);
    QTRY_VERIFY_WITH_TIMEOUT(!accounts->busy(), 5000);
    QVERIFY2(accounts->error().isEmpty(), qPrintable(accounts->error()));
    QCOMPARE(QUrlQuery(m_browser.last).queryItemValue(u"login_hint"_s, QUrl::FullyDecoded), kMe.id);
    // The same account, with its new sign-in.
    QCOMPARE(accounts->accounts().size(), 1);
    QCOMPARE(m_tokens->secrets.value(kMe.id), u"rt-2"_s);
}

void TestAccounts::signingInAgainSyncsAndHoldsOffOtherActions()
{
    AccountsController *accounts = AccountsController::instance();
    bool sawSaving = false;
    // While the account is saved nothing else may start, Cancel included.
    const auto saving = connect(accounts, &AccountsController::stateChanged, this, [&] {
        if (sawSaving || accounts->status() != u"Saving the account..."_s)
            return;
        sawSaving = true;
        QVERIFY(accounts->busy());
        QVERIFY(!accounts->signingIn());
        accounts->remove(kMe.id);
        accounts->connectGoogle();
        accounts->cancel();
    });
    const int opened = m_browser.opened;
    signIn(200, R"({"access_token":"at-1","refresh_token":"rt-1","expires_in":3600})");
    QSignalSpy refreshed(m_source.get(), &CalendarSource::changed);

    QTRY_VERIFY_WITH_TIMEOUT(!accounts->busy(), 5000);
    disconnect(saving);
    QVERIFY(sawSaving);
    QVERIFY2(accounts->error().isEmpty(), qPrintable(accounts->error()));
    QCOMPARE(m_browser.opened, opened + 1);
    QCOMPARE(accounts->accounts().size(), 1);
    QCOMPARE(m_tokens->secrets.value(kMe.id), u"rt-1"_s);
    // The list did not change, so only the sign-in itself can have refreshed.
    QVERIFY(!refreshed.isEmpty());
}

void TestAccounts::failedSignInEndsTheAttempt()
{
    AccountsController *accounts = AccountsController::instance();
    // The dialog only rereads busy when told the state changed.
    bool busyWhenFailed = true;
    const auto failed = connect(accounts, &AccountsController::stateChanged, this, [&] {
        if (!accounts->error().isEmpty())
            busyWhenFailed = accounts->busy() || accounts->signingIn();
    });
    signIn(400, R"({"error":"invalid_grant"})");
    QVERIFY(accounts->signingIn());

    QTRY_VERIFY_WITH_TIMEOUT(!accounts->error().isEmpty(), 5000);
    disconnect(failed);
    QVERIFY(!busyWhenFailed);
    QCOMPARE(accounts->accounts().size(), 1);
}

QTEST_MAIN(TestAccounts)
#include "tst_accounts.moc"
