#include "AccountsController.h"
#include "FakeTokenStore.h"

#include "callie/AccountStore.h"
#include "callie/GoogleCache.h"
#include "callie/GoogleSource.h"

#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestAccounts : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void listsAndRemovesAccounts();
    void sampleModeTouchesNothing();
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

QTEST_MAIN(TestAccounts)
#include "tst_accounts.moc"
