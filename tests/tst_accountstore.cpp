#include "callie/AccountStore.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;

namespace {

/// The stored accounts, failing the test if the file cannot be read.
QList<Account> listed(AccountStore &store)
{
    QList<Account> accounts;
    if (!store.load(accounts))
        qFatal("account list unreadable: %s", qPrintable(store.errorString()));
    return accounts;
}

} // namespace

class TestAccountStore : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void missingFileIsEmpty();
    void addCreatesDirectoriesAndPersists();
    void addIsIdempotent();
    void removeDropsOnlyThatAccount();
    void corruptFileIsNeverOverwritten();
    void loadReportsCorruptFile();

private:
    QTemporaryDir m_dir;
    QString path(const char *name) const { return m_dir.filePath(QString::fromLatin1(name)); }
};

void TestAccountStore::missingFileIsEmpty()
{
    AccountStore store(path("missing/accounts.json"));
    QVERIFY(listed(store).isEmpty());
}

void TestAccountStore::addCreatesDirectoriesAndPersists()
{
    const QString file = path("nested/dir/accounts.json");
    QVERIFY(AccountStore(file).add({QStringLiteral("google"), QStringLiteral("a@example.com")}));

    AccountStore reopened(file);
    const QList<Account> accounts = listed(reopened);
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
    QCOMPARE(listed(store).size(), 1);
}

void TestAccountStore::removeDropsOnlyThatAccount()
{
    AccountStore store(path("remove.json"));
    const Account a{QStringLiteral("google"), QStringLiteral("a@example.com")};
    const Account b{QStringLiteral("google"), QStringLiteral("b@example.com")};
    QVERIFY(store.add(a));
    QVERIFY(store.add(b));

    QVERIFY(store.remove(a));
    QCOMPARE(listed(store), QList<Account>{b});
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

void TestAccountStore::loadReportsCorruptFile()
{
    QList<Account> accounts;
    QVERIFY(AccountStore(path("not-there.json")).load(accounts));
    QVERIFY(accounts.isEmpty());

    QFile file(path("broken.json"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{ not json");
    file.close();
    AccountStore store(file.fileName());
    QVERIFY(!store.load(accounts));
    QVERIFY(store.errorString().contains(QStringLiteral("not valid")));
}

QTEST_GUILESS_MAIN(TestAccountStore)
#include "tst_accountstore.moc"
