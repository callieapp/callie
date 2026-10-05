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
        QTimer::singleShot(0, [=, this] {
            secrets.insert(account.id, secret);
            done({});
        });
    }

    void read(const callie::Account &account, Loaded loaded) override
    {
        ++reads;
        QTimer::singleShot(0, [=, this] { loaded(secrets.value(account.id), failWith); });
    }

    void remove(const callie::Account &account, Done done) override
    {
        QTimer::singleShot(0, [=, this] {
            secrets.remove(account.id);
            done({});
        });
    }

    QHash<QString, QString> secrets;
    QString failWith;
    int reads = 0;
};
