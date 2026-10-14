#include "AuthDriver.h"
#include "FakeCalDavServer.h"

#include "callie/AccountManager.h"
#include "callie/AccountStore.h"
#include "callie/CalDavClient.h"
#include "callie/CalDavSource.h"
#include "callie/ContactBook.h"
#include "callie/GoogleCache.h"
#include "callie/GoogleCalendarApi.h"
#include "callie/TokenStore.h"

#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

/// The stored accounts, failing the test if the file cannot be read.
QList<Account> listed(AccountStore &store)
{
    QList<Account> accounts;
    if (!store.load(accounts))
        qFatal("account list unreadable: %s", qPrintable(store.errorString()));
    return accounts;
}

const GoogleClientConfig kClient{QStringLiteral("test-id"), QStringLiteral("test-secret")};
const Account kAccount{QStringLiteral("google"), QStringLiteral("me@example.com")};

/// An in-memory keyring that can be told to fail, and that records what the
/// account list held at the moment each token was written.
class FakeTokenStore : public TokenStore
{
public:
    explicit FakeTokenStore(AccountStore &store) : m_store(store) {}

    void write(const Account &account, const QString &secret, Done done) override
    {
        listedWhenWritten = listed(m_store).contains(account);
        complete([=, this] {
            if (failWith.isEmpty())
                secrets.insert(account.id, secret);
            done(failWith);
        });
    }

    void read(const Account &account, Loaded loaded) override
    {
        complete([=, this] { loaded(secrets.value(account.id), failWith); });
    }

    void remove(const Account &account, Done done) override
    {
        complete([=, this] {
            if (failWith.isEmpty())
                secrets.remove(account.id);
            done(failWith);
        });
    }

    QString failWith;
    QHash<QString, QString> secrets;
    bool listedWhenWritten = false;

private:
    // The real keyring never answers synchronously, so neither does the fake.
    // The answers belong to m_context, so a store deleted first never gives them.
    void complete(std::function<void()> callback)
    {
        QTimer::singleShot(0, &m_context, std::move(callback));
    }

    AccountStore &m_store;
    QObject m_context;
};

} // namespace

class TestAccountManager : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();

    void connectStoresTokenBeforeListingAccount();
    void keyringFailureLeavesNoAccount();
    void listFailureRemovesStoredToken();
    void apiFailureWritesNothing();
    void signInFailureWritesNothing();
    void removeForgetsTokenThenAccount();
    void removeKeepsAccountWhenKeyringFails();
    void removeUnknownAccountFails();
    void removeClearsCachedEvents();
    void removeForgetsContacts();
    void removeKeepsAccountWhenCacheFails();
    void removeReportsCorruptList();
    void connectRefusesCorruptListBeforeSignIn();
    void calDavChecksThePasswordFirst();
    void calDavSignInAgainKeepsTheListing();
    void removingCalDavForgetsItsCalendars();
    void calDavFailuresStoreNothing();
    void calDavSignInToAnotherServerMovesIt();
    void calDavCopiesThatStayKeepTheAccount();
    void calDavIdsNameTheServer();

private:
    void corruptAccountList();

    /// Runs connectGoogle against fake Google endpoints that answer with
    /// `tokenStatus`/`tokenBody` and `apiStatus`/`apiBody`.
    void runConnect(int tokenStatus, const QByteArray &tokenBody, int apiStatus,
                    const QByteArray &apiBody);

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<AccountStore> m_store;
    std::unique_ptr<FakeTokenStore> m_tokens;
    std::unique_ptr<AccountManager> m_manager;
};

void TestAccountManager::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_store = std::make_unique<AccountStore>(m_dir->filePath(QStringLiteral("accounts.json")));
    m_tokens = std::make_unique<FakeTokenStore>(*m_store);
    m_manager = std::make_unique<AccountManager>(*m_tokens, *m_store);
}

void TestAccountManager::runConnect(int tokenStatus, const QByteArray &tokenBody, int apiStatus,
                                    const QByteArray &apiBody)
{
    FakeHttpServer tokenServer;
    tokenServer.respond(tokenStatus, tokenBody);
    FakeHttpServer apiServer;
    apiServer.respond(apiStatus, apiBody);

    QNetworkAccessManager network;
    GoogleAuth auth(kClient);
    AuthDriver::useTokenServer(auth, tokenServer);
    GoogleCalendarApi api(&network);
    api.setBaseUrl(apiServer.url(QStringLiteral("/calendar/v3/")));
    api.setPeopleBaseUrl(apiServer.url(QStringLiteral("/people/v1/")));

    QSignalSpy connected(m_manager.get(), &AccountManager::connected);
    QSignalSpy failed(m_manager.get(), &AccountManager::failed);
    AuthDriver::approveNextAuthorization(network, auth);
    m_manager->connectGoogle(auth, api);

    QTRY_VERIFY_WITH_TIMEOUT(connected.size() + failed.size() > 0, 5000);
    // Nothing may report a second outcome afterwards.
    QTest::qWait(200);
    QCOMPARE(connected.size() + failed.size(), 1);
}

void TestAccountManager::connectStoresTokenBeforeListingAccount()
{
    QSignalSpy connected(m_manager.get(), &AccountManager::connected);
    runConnect(200, R"({"access_token":"at-1","refresh_token":"rt-1","expires_in":3600})", 200,
               R"({"id":"me@example.com"})");

    QCOMPARE(connected.size(), 1);
    QCOMPARE(connected.first().first().value<Account>(), kAccount);
    QCOMPARE(m_tokens->secrets.value(kAccount.id), QStringLiteral("rt-1"));
    QVERIFY2(!m_tokens->listedWhenWritten, "account was listed before its token was stored");
    QCOMPARE(listed(*m_store), QList<Account>{kAccount});
}

void TestAccountManager::keyringFailureLeavesNoAccount()
{
    m_tokens->failWith = QStringLiteral("keyring is locked");
    QSignalSpy failed(m_manager.get(), &AccountManager::failed);
    runConnect(200, R"({"access_token":"at-1","refresh_token":"rt-1","expires_in":3600})", 200,
               R"({"id":"me@example.com"})");

    QCOMPARE(failed.size(), 1);
    QVERIFY(failed.first().first().toString().contains(QStringLiteral("keyring is locked")));
    QVERIFY(listed(*m_store).isEmpty());
}

void TestAccountManager::listFailureRemovesStoredToken()
{
    // A regular file where the config directory should be makes the list
    // unwritable after the keyring write has already succeeded.
    const QString blocker = m_dir->filePath(QStringLiteral("blocker"));
    QFile file(blocker);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    m_store = std::make_unique<AccountStore>(blocker + QStringLiteral("/accounts.json"));
    m_tokens = std::make_unique<FakeTokenStore>(*m_store);
    m_manager = std::make_unique<AccountManager>(*m_tokens, *m_store);

    QSignalSpy failed(m_manager.get(), &AccountManager::failed);
    runConnect(200, R"({"access_token":"at-1","refresh_token":"rt-1","expires_in":3600})", 200,
               R"({"id":"me@example.com"})");

    QCOMPARE(failed.size(), 1);
    QVERIFY2(m_tokens->secrets.isEmpty(), "token left in the keyring with no listed account");
}

void TestAccountManager::apiFailureWritesNothing()
{
    QSignalSpy failed(m_manager.get(), &AccountManager::failed);
    runConnect(200, R"({"access_token":"at-1","refresh_token":"rt-1","expires_in":3600})", 401,
               R"({"error":{"code":401,"message":"Invalid Credentials"}})");

    QCOMPARE(failed.size(), 1);
    QCOMPARE(failed.first().first().toString(), QStringLiteral("Invalid Credentials"));
    QVERIFY(m_tokens->secrets.isEmpty());
    QVERIFY(listed(*m_store).isEmpty());
}

void TestAccountManager::signInFailureWritesNothing()
{
    QSignalSpy failed(m_manager.get(), &AccountManager::failed);
    runConnect(400, R"({"error":"invalid_grant","error_description":"Bad Request"})", 200,
               R"({"id":"me@example.com"})");

    QCOMPARE(failed.size(), 1);
    QVERIFY(m_tokens->secrets.isEmpty());
    QVERIFY(listed(*m_store).isEmpty());
}

void TestAccountManager::removeForgetsTokenThenAccount()
{
    QVERIFY(m_store->add(kAccount));
    m_tokens->secrets.insert(kAccount.id, QStringLiteral("rt-1"));
    QSignalSpy removed(m_manager.get(), &AccountManager::removed);

    m_manager->remove(kAccount);

    QVERIFY(removed.wait(2000));
    QVERIFY(m_tokens->secrets.isEmpty());
    QVERIFY(listed(*m_store).isEmpty());
}

void TestAccountManager::removeKeepsAccountWhenKeyringFails()
{
    QVERIFY(m_store->add(kAccount));
    m_tokens->failWith = QStringLiteral("keyring is locked");
    QSignalSpy failed(m_manager.get(), &AccountManager::failed);

    m_manager->remove(kAccount);

    QVERIFY(failed.wait(2000));
    QCOMPARE(listed(*m_store), QList<Account>{kAccount});
}

void TestAccountManager::removeUnknownAccountFails()
{
    QSignalSpy failed(m_manager.get(), &AccountManager::failed);
    QSignalSpy removed(m_manager.get(), &AccountManager::removed);

    m_manager->remove(kAccount);

    QVERIFY(failed.wait(2000));
    QCOMPARE(removed.size(), 0);
}

void TestAccountManager::removeClearsCachedEvents()
{
    GoogleCache cache(m_dir->filePath(QStringLiteral("google.sqlite")));
    QVERIFY(cache.open());
    GoogleCalendar calendar;
    calendar.id = kAccount.id;
    QVERIFY(cache.setCalendars(kAccount, {calendar}));
    AccountManager manager(*m_tokens, *m_store, &cache);
    QVERIFY(m_store->add(kAccount));
    QSignalSpy removed(&manager, &AccountManager::removed);

    manager.remove(kAccount);

    QVERIFY(removed.wait(2000));
    QVERIFY(cache.calendars(kAccount).isEmpty());
    QVERIFY(listed(*m_store).isEmpty());
}

void TestAccountManager::removeForgetsContacts()
{
    ContactBook contacts({});
    contacts.setContacts(kAccount.id, {{QStringLiteral("Pat"), QStringLiteral("pat@example.com")}});
    AccountManager manager(*m_tokens, *m_store);
    manager.setContacts(&contacts);
    QVERIFY(m_store->add(kAccount));
    QSignalSpy removed(&manager, &AccountManager::removed);

    manager.remove(kAccount);

    QVERIFY(removed.wait(2000));
    QVERIFY(contacts.suggest(QStringLiteral("pat"), {}).isEmpty());
}

void TestAccountManager::removeKeepsAccountWhenCacheFails()
{
    // Never opened, so every write fails.
    GoogleCache cache(m_dir->filePath(QStringLiteral("google.sqlite")));
    AccountManager manager(*m_tokens, *m_store, &cache);
    QVERIFY(m_store->add(kAccount));
    QSignalSpy failed(&manager, &AccountManager::failed);
    QSignalSpy removed(&manager, &AccountManager::removed);

    manager.remove(kAccount);

    QVERIFY(failed.wait(2000));
    QCOMPARE(removed.size(), 0);
    QCOMPARE(listed(*m_store), QList<Account>{kAccount});
}

void TestAccountManager::corruptAccountList()
{
    QFile file(m_dir->filePath(QStringLiteral("accounts.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{ not json");
}

void TestAccountManager::removeReportsCorruptList()
{
    corruptAccountList();
    QSignalSpy failed(m_manager.get(), &AccountManager::failed);

    m_manager->remove(kAccount);

    QVERIFY(failed.wait(2000));
    QVERIFY2(failed.first().first().toString().contains(QStringLiteral("not valid")),
             qPrintable(failed.first().first().toString()));
}

void TestAccountManager::connectRefusesCorruptListBeforeSignIn()
{
    corruptAccountList();
    QNetworkAccessManager network;
    GoogleAuth auth(kClient);
    GoogleCalendarApi api(&network);
    QSignalSpy browser(&auth, &GoogleAuth::authorizeUrlReady);
    QSignalSpy failed(m_manager.get(), &AccountManager::failed);

    m_manager->connectGoogle(auth, api);

    QVERIFY(failed.wait(2000));
    QCOMPARE(browser.size(), 0);
    QVERIFY(m_tokens->secrets.isEmpty());
}

void TestAccountManager::calDavChecksThePasswordFirst()
{
    FakeCalDavServer server;
    server.addCalendar("work", "Work");
    QNetworkAccessManager network;
    CalDavClient client(&network);
    QSignalSpy connected(m_manager.get(), &AccountManager::connected);
    QSignalSpy failed(m_manager.get(), &AccountManager::failed);
    Account account;
    connect(m_manager.get(), &AccountManager::connected, this,
            [&account](const Account &made) { account = made; });

    // A wrong password stores and lists nothing.
    m_manager->connectCalDav(client, server.url(), u"me"_s, u"guess"_s);
    QVERIFY(failed.wait());
    QVERIFY(failed.first().first().toString().contains(u"app password"_s));
    QVERIFY(m_tokens->secrets.isEmpty());
    QList<Account> accounts;
    QVERIFY(m_store->load(accounts));
    QVERIFY(accounts.isEmpty());

    m_manager->connectCalDav(client, server.url(), u"me"_s, u"app-password"_s);
    QVERIFY(connected.wait());
    QCOMPARE(account.provider, u"caldav"_s);
    QCOMPARE(account.id, u"me@127.0.0.1"_s);
    QCOMPARE(m_tokens->secrets.value(account.id), u"app-password"_s);
    QVERIFY(m_store->load(accounts));
    QCOMPARE(accounts.size(), 1);
    QCOMPARE(accounts.first().server, server.url().toString());
    QCOMPARE(accounts.first().user, u"me"_s);
}

void TestAccountManager::calDavSignInAgainKeepsTheListing()
{
    FakeCalDavServer server;
    server.addCalendar("work", "Work");
    QNetworkAccessManager network;
    CalDavClient client(&network);
    QSignalSpy connected(m_manager.get(), &AccountManager::connected);
    m_manager->connectCalDav(client, server.url(), u"me"_s, u"app-password"_s);
    QVERIFY(connected.wait());
    server.password = "new-password";
    m_manager->connectCalDav(client, server.url(), u"me"_s, u"new-password"_s);
    QVERIFY(connected.wait());
    QList<Account> accounts;
    QVERIFY(m_store->load(accounts));
    QCOMPARE(accounts.size(), 1);
    QCOMPARE(m_tokens->secrets.value(u"me@127.0.0.1"_s), u"new-password"_s);
}

void TestAccountManager::removingCalDavForgetsItsCalendars()
{
    const Account account{u"caldav"_s, u"me@example.com"_s, u"https://dav.example.com/"_s, u"me"_s};
    QVERIFY(m_store->add(account));
    m_tokens->secrets.insert(account.id, u"app-password"_s);
    CalDavSource caldav(m_dir->filePath(u"caldav"_s), nullptr, nullptr);
    QVERIFY(QDir().mkpath(m_dir->filePath(u"caldav/me@example.com/work"_s)));
    m_manager->setCalDav(&caldav);
    // A Google account of the same address keeps its contacts.
    ContactBook contacts(m_dir->filePath(u"contacts.json"_s));
    contacts.setContacts(u"me@example.com"_s, {Contact{u"Alex"_s, u"alex@example.com"_s}});
    m_manager->setContacts(&contacts);
    QSignalSpy removed(m_manager.get(), &AccountManager::removed);
    m_manager->remove(account);
    QVERIFY(removed.wait());
    QVERIFY(!QDir(m_dir->filePath(u"caldav/me@example.com"_s)).exists());
    QCOMPARE(contacts.suggest(u"alex"_s, {}).size(), 1);
}

void TestAccountManager::calDavFailuresStoreNothing()
{
    QNetworkAccessManager network;
    CalDavClient client(&network);
    QSignalSpy failed(m_manager.get(), &AccountManager::failed);
    QList<Account> accounts;

    // A server with no calendars for the account.
    FakeCalDavServer empty;
    m_manager->connectCalDav(client, empty.url(), u"me"_s, u"app-password"_s);
    QVERIFY(failed.wait());
    QVERIFY(failed.last().first().toString().contains(u"no calendars"_s));

    FakeCalDavServer server;
    server.addCalendar("work", "Work");
    // A keyring that will not take the password.
    m_tokens->failWith = u"locked"_s;
    m_manager->connectCalDav(client, server.url(), u"me"_s, u"app-password"_s);
    QVERIFY(failed.wait());
    QVERIFY(failed.last().first().toString().contains(u"locked"_s));
    QVERIFY(m_store->load(accounts));
    QVERIFY(accounts.isEmpty());
    m_tokens->failWith.clear();

    // A list that cannot be written: the password stored goes again.
    const QString blocker = m_dir->filePath(u"blocker"_s);
    QFile file(blocker);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    m_store = std::make_unique<AccountStore>(blocker + u"/accounts.json"_s);
    m_tokens = std::make_unique<FakeTokenStore>(*m_store);
    m_manager = std::make_unique<AccountManager>(*m_tokens, *m_store);
    QSignalSpy listFailed(m_manager.get(), &AccountManager::failed);
    m_manager->connectCalDav(client, server.url(), u"me"_s, u"app-password"_s);
    QVERIFY(listFailed.wait());
    QTRY_VERIFY(m_tokens->secrets.isEmpty());
}

void TestAccountManager::calDavSignInToAnotherServerMovesIt()
{
    FakeCalDavServer first;
    first.addCalendar("work", "Work");
    FakeCalDavServer second;
    second.addCalendar("home", "Home");
    QNetworkAccessManager network;
    CalDavClient client(&network);
    QSignalSpy connected(m_manager.get(), &AccountManager::connected);
    m_manager->connectCalDav(client, first.url(), u"me"_s, u"app-password"_s);
    QVERIFY(connected.wait());
    // Same user and host, another server: the account follows it.
    m_manager->connectCalDav(client, second.url(), u"me"_s, u"app-password"_s);
    QVERIFY(connected.wait());
    QList<Account> accounts;
    QVERIFY(m_store->load(accounts));
    QCOMPARE(accounts.size(), 1);
    QCOMPARE(accounts.first().server, second.url().toString());

    // A password typed into the address is not kept with the account.
    QUrl typed = second.url();
    typed.setUserInfo(u"me:app-password"_s);
    m_manager->connectCalDav(client, typed, u"me"_s, u"app-password"_s);
    QVERIFY(connected.wait());
    QVERIFY(m_store->load(accounts));
    QVERIFY(!accounts.first().server.contains(u"app-password"_s));
}

void TestAccountManager::calDavCopiesThatStayKeepTheAccount()
{
    const Account account{u"caldav"_s, u"me@example.com"_s, u"https://dav.example.com/"_s, u"me"_s};
    QVERIFY(m_store->add(account));
    m_tokens->secrets.insert(account.id, u"app-password"_s);
    // Copies that cannot be deleted, as on a full or read-only disk.
    struct Stuck : CalDavSource
    {
        using CalDavSource::CalDavSource;
        bool forget(const QString &, QString *error) override
        {
            *error = u"read-only file system"_s;
            return false;
        }
    } caldav(m_dir->filePath(u"caldav"_s), nullptr, nullptr);
    m_manager->setCalDav(&caldav);
    QSignalSpy failed(m_manager.get(), &AccountManager::failed);
    m_manager->remove(account);
    QVERIFY(failed.wait());
    QVERIFY(failed.first().first().toString().contains(u"read-only"_s));
    QList<Account> accounts;
    QVERIFY(m_store->load(accounts));
    QCOMPARE(accounts.size(), 1);
}

void TestAccountManager::calDavIdsNameTheServer()
{
    // One login at two services is two accounts.
    const QString fastmail =
        AccountManager::calDavId(QUrl(u"https://caldav.fastmail.com/"_s), u"me@example.com"_s);
    const QString icloud =
        AccountManager::calDavId(QUrl(u"https://caldav.icloud.com/"_s), u"me@example.com"_s);
    QVERIFY(fastmail != icloud);
    QCOMPARE(fastmail, u"me@example.com (caldav.fastmail.com)"_s);
    QCOMPARE(AccountManager::calDavId(QUrl(u"https://cloud.example.com/"_s), u" sam "_s),
             u"sam@cloud.example.com"_s);
}

QTEST_GUILESS_MAIN(TestAccountManager)
#include "tst_accountmanager.moc"
