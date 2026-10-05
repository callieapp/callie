#include "callie/TokenStore.h"

#include <qt6keychain/keychain.h>

namespace callie {

namespace {

const QString kService = QStringLiteral("org.callieapp.Callie");

QString keyFor(const Account &account)
{
    return account.provider + u'/' + account.id;
}

template<typename Job>
Job *makeJob(const Account &account)
{
    auto *job = new Job(kService);
    job->setAutoDelete(true);
    job->setInsecureFallback(false);
    job->setKey(keyFor(account));
    return job;
}

} // namespace

void KeychainTokenStore::write(const Account &account, const QString &secret, Done done)
{
    auto *job = makeJob<QKeychain::WritePasswordJob>(account);
    job->setTextData(secret);
    QObject::connect(job, &QKeychain::Job::finished, [done](QKeychain::Job *j) {
        done(j->error() == QKeychain::NoError ? QString() : j->errorString());
    });
    job->start();
}

void KeychainTokenStore::read(const Account &account, Loaded loaded)
{
    auto *job = makeJob<QKeychain::ReadPasswordJob>(account);
    QObject::connect(job, &QKeychain::Job::finished, [loaded](QKeychain::Job *j) {
        auto *read = static_cast<QKeychain::ReadPasswordJob *>(j);
        if (read->error() == QKeychain::NoError)
            loaded(read->textData(), QString());
        else
            loaded(QString(), read->errorString());
    });
    job->start();
}

void KeychainTokenStore::remove(const Account &account, Done done)
{
    auto *job = makeJob<QKeychain::DeletePasswordJob>(account);
    QObject::connect(job, &QKeychain::Job::finished, [done](QKeychain::Job *j) {
        const bool ok = j->error() == QKeychain::NoError || j->error() == QKeychain::EntryNotFound;
        done(ok ? QString() : j->errorString());
    });
    job->start();
}

} // namespace callie
