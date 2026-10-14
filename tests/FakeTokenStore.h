#pragma once

#include "callie/TokenStore.h"

#include <QHash>
#include <QTimer>

/// An in-memory TokenStore that, like the real keyring, never answers synchronously.
class FakeTokenStore : public callie::TokenStore
{
public:
    void write(const callie::Account &account, const QString &secret, Done done) override
    {
        ++writes;
        QTimer::singleShot(0, &m_context, [=, this] {
            if (writeFailure.isEmpty())
                secrets.insert(account.id, secret);
            done(writeFailure);
        });
    }

    void read(const callie::Account &account, Loaded loaded) override
    {
        ++reads;
        QTimer::singleShot(0, &m_context,
                           [=, this] { loaded(secrets.value(account.id), failWith); });
    }

    void remove(const callie::Account &account, Done done) override
    {
        QTimer::singleShot(0, &m_context, [=, this] {
            secrets.remove(account.id);
            done({});
        });
    }

    QHash<QString, QString> secrets;
    QString failWith;
    /// Every write fails with this, when set.
    QString writeFailure;
    int reads = 0;
    int writes = 0;

private:
    /// Owns the pending answers, so a store deleted before they come never
    /// gives them.
    QObject m_context;
};
