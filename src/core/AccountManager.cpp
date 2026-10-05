#include "callie/AccountManager.h"

#include "callie/AccountStore.h"
#include "callie/GoogleAuth.h"
#include "callie/GoogleCalendarApi.h"
#include "callie/Logging.h"
#include "callie/TokenStore.h"

#include <QTimer>

#include <memory>

namespace callie {

AccountManager::AccountManager(TokenStore &tokens, AccountStore &store, QObject *parent)
    : QObject(parent), m_tokens(tokens), m_store(store)
{}

void AccountManager::connectGoogle(GoogleAuth &auth, GoogleCalendarApi &api)
{
    // Checked before the browser opens: an account list that cannot be read
    // would otherwise only fail after the user has already signed in.
    QList<Account> existing;
    if (!m_store.load(existing)) {
        QTimer::singleShot(0, this,
                           [this, message = m_store.errorString()] { Q_EMIT failed(message); });
        return;
    }

    // The attempt owns this call's connections, and the flag makes sure only the
    // first outcome is reported even if the flow signals more than once.
    auto *attempt = new QObject(this);
    auto finished = std::make_shared<bool>(false);
    const auto finish = [attempt, finished] {
        if (*finished)
            return false;
        *finished = true;
        attempt->deleteLater();
        return true;
    };
    const auto fail = [this, finish](const QString &message) {
        if (finish())
            Q_EMIT failed(message);
    };

    connect(&auth, &GoogleAuth::failed, attempt, fail);
    connect(
        &auth, &GoogleAuth::granted, attempt,
        [this, &api, finish, fail](const GoogleTokens &tokens) {
            api.fetchPrimaryCalendarId(tokens.accessToken, [this, finish, fail,
                                                            tokens](const QString &id,
                                                                    const QString &error) {
                if (!error.isEmpty()) {
                    fail(error);
                    return;
                }
                const Account account{QStringLiteral("google"), id};
                m_tokens.write(
                    account, tokens.refreshToken,
                    [this, finish, fail, account](const QString &keyring) {
                        if (!keyring.isEmpty())
                            fail(tr("could not store the token: %1").arg(keyring));
                        else if (!m_store.add(account)) {
                            // Undo the keyring write, or the token would stay behind with no
                            // listed account, where remove() cannot reach it.
                            const QString listError = m_store.errorString();
                            m_tokens.remove(account, [fail, listError](const QString &cleanup) {
                                fail(cleanup.isEmpty()
                                         ? listError
                                         : tr("%1, and the stored token could not be removed: %2")
                                               .arg(listError, cleanup));
                            });
                        } else if (finish()) {
                            qCInfo(lcAccounts) << "connected" << account.provider << account.id;
                            Q_EMIT connected(account);
                        }
                    });
            });
        });

    auth.authorize();
}

void AccountManager::remove(const Account &account)
{
    QList<Account> accounts;
    QString problem;
    if (!m_store.load(accounts))
        problem = m_store.errorString();
    else if (!accounts.contains(account))
        problem = tr("no %1 account '%2'").arg(account.provider, account.id);
    if (!problem.isEmpty()) {
        // Asynchronous like every other outcome, so callers handle one shape.
        QTimer::singleShot(0, this, [this, problem] { Q_EMIT failed(problem); });
        return;
    }

    m_tokens.remove(account, [this, account](const QString &keyring) {
        if (!keyring.isEmpty())
            Q_EMIT failed(tr("could not remove the token: %1").arg(keyring));
        else if (!m_store.remove(account))
            Q_EMIT failed(m_store.errorString());
        else {
            qCInfo(lcAccounts) << "removed" << account.provider << account.id;
            Q_EMIT removed(account);
        }
    });
}

} // namespace callie
