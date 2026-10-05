#include "GoogleAuthDriver.h"

#include "callie/AccountManager.h"
#include "callie/AccountStore.h"
#include "callie/GoogleCalendarApi.h"
#include "callie/TokenStore.h"

#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace callie;

namespace {

const GoogleClientConfig kClient{QStringLiteral("test-id"), QStringLiteral("test-secret")};
const Account kAccount{QStringLiteral("google"), QStringLiteral("me@example.com")};

/// An in-memory keyring that can be told to fail, and that records what the
/// account list held at the moment each token was written.
class FakeTokenStore : public TokenStore
{
public:
    explicit FakeTokenStore(const AccountStore &store) : m_store(store) {}

    void write(const Account &account, const QString &secret, Done done) override
    {
        listedWhenWritten = m_store.contains(account);
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
    static void complete(std::function<void()> callback) { QTimer::singleShot(0, callback); }

    const AccountStore &m_store;
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

private:
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
    GoogleAuthDriver::useTokenServer(auth, tokenServer);
    GoogleCalendarApi api(&network);
    api.setBaseUrl(apiServer.url(QStringLiteral("/calendar/v3/")));

    QSignalSpy connected(m_manager.get(), &AccountManager::connected);
    QSignalSpy failed(m_manager.get(), &AccountManager::failed);
    GoogleAuthDriver::approveNextAuthorization(network, auth);
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
    QCOMPARE(m_store->accounts(), QList<Account>{kAccount});
}

void TestAccountManager::keyringFailureLeavesNoAccount()
{
    m_tokens->failWith = QStringLiteral("keyring is locked");
    QSignalSpy failed(m_manager.get(), &AccountManager::failed);
    runConnect(200, R"({"access_token":"at-1","refresh_token":"rt-1","expires_in":3600})", 200,
               R"({"id":"me@example.com"})");

    QCOMPARE(failed.size(), 1);
    QVERIFY(failed.first().first().toString().contains(QStringLiteral("keyring is locked")));
    QVERIFY(m_store->accounts().isEmpty());
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
    QVERIFY(m_store->accounts().isEmpty());
}

void TestAccountManager::signInFailureWritesNothing()
{
    QSignalSpy failed(m_manager.get(), &AccountManager::failed);
    runConnect(400, R"({"error":"invalid_grant","error_description":"Bad Request"})", 200,
               R"({"id":"me@example.com"})");

    QCOMPARE(failed.size(), 1);
    QVERIFY(m_tokens->secrets.isEmpty());
    QVERIFY(m_store->accounts().isEmpty());
}

void TestAccountManager::removeForgetsTokenThenAccount()
{
    QVERIFY(m_store->add(kAccount));
    m_tokens->secrets.insert(kAccount.id, QStringLiteral("rt-1"));
    QSignalSpy removed(m_manager.get(), &AccountManager::removed);

    m_manager->remove(kAccount);

    QVERIFY(removed.wait(2000));
    QVERIFY(m_tokens->secrets.isEmpty());
    QVERIFY(m_store->accounts().isEmpty());
}

void TestAccountManager::removeKeepsAccountWhenKeyringFails()
{
    QVERIFY(m_store->add(kAccount));
    m_tokens->failWith = QStringLiteral("keyring is locked");
    QSignalSpy failed(m_manager.get(), &AccountManager::failed);

    m_manager->remove(kAccount);

    QVERIFY(failed.wait(2000));
    QCOMPARE(m_store->accounts(), QList<Account>{kAccount});
}

void TestAccountManager::removeUnknownAccountFails()
{
    QSignalSpy failed(m_manager.get(), &AccountManager::failed);
    QSignalSpy removed(m_manager.get(), &AccountManager::removed);

    m_manager->remove(kAccount);

    QVERIFY(failed.wait(2000));
    QCOMPARE(removed.size(), 0);
}

QTEST_GUILESS_MAIN(TestAccountManager)
#include "tst_accountmanager.moc"
