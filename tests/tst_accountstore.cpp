#include "callie/AccountStore.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;

class TestAccountStore : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void missingFileIsEmpty();
    void addCreatesDirectoriesAndPersists();
    void addIsIdempotent();
    void removeDropsOnlyThatAccount();
    void corruptFileIsEmpty();
    void corruptFileIsNeverOverwritten();

private:
    QTemporaryDir m_dir;
    QString path(const char *name) const { return m_dir.filePath(QString::fromLatin1(name)); }
};

void TestAccountStore::missingFileIsEmpty()
{
    QVERIFY(AccountStore(path("missing/accounts.json")).accounts().isEmpty());
}

void TestAccountStore::addCreatesDirectoriesAndPersists()
{
    const QString file = path("nested/dir/accounts.json");
    QVERIFY(AccountStore(file).add({QStringLiteral("google"), QStringLiteral("a@example.com")}));

    const QList<Account> accounts = AccountStore(file).accounts();
    QCOMPARE(accounts.size(), 1);
    QCOMPARE(accounts.first().provider, QStringLiteral("google"));
    QCOMPARE(accounts.first().id, QStringLiteral("a@example.com"));
}

void TestAccountStore::addIsIdempotent()
{
    AccountStore store(path("idempotent.json"));
    const Account account{QStringLiteral("google"), QStringLiteral("a@example.com")};
    QVERIFY(store.add(account));
    QVERIFY(store.add(account));
    QCOMPARE(store.accounts().size(), 1);
}

void TestAccountStore::removeDropsOnlyThatAccount()
{
    AccountStore store(path("remove.json"));
    const Account a{QStringLiteral("google"), QStringLiteral("a@example.com")};
    const Account b{QStringLiteral("google"), QStringLiteral("b@example.com")};
    QVERIFY(store.add(a));
    QVERIFY(store.add(b));

    QVERIFY(store.remove(a));
    QCOMPARE(store.accounts(), QList<Account>{b});
    QVERIFY(!store.contains(a));
}

void TestAccountStore::corruptFileIsEmpty()
{
    QFile file(path("corrupt.json"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{ not json");
    file.close();
    QVERIFY(AccountStore(file.fileName()).accounts().isEmpty());
}

void TestAccountStore::corruptFileIsNeverOverwritten()
{
    const QByteArray corrupt = "{ \"accounts\": [ { \"provider\": \"google\", ";
    QFile file(path("hand-edited.json"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(corrupt);
    file.close();

    AccountStore store(file.fileName());
    QVERIFY(!store.add({QStringLiteral("google"), QStringLiteral("new@example.com")}));
    QVERIFY(!store.remove({QStringLiteral("google"), QStringLiteral("a@example.com")}));
    QVERIFY(!store.errorString().isEmpty());

    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), corrupt);
}

QTEST_GUILESS_MAIN(TestAccountStore)
#include "tst_accountstore.moc"
