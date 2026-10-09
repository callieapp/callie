#include "AccountsController.h"

#include "callie/AccountManager.h"
#include "callie/AccountStore.h"
#include "callie/GoogleAuth.h"
#include "callie/GoogleCalendarApi.h"
#include "callie/GoogleSource.h"
#include "callie/Logging.h"

#include <QCoreApplication>
#include <QDesktopServices>

using namespace Qt::StringLiterals;

namespace callie {

AccountsController::AccountsController(QObject *parent) : QObject(parent) {}

AccountsController *AccountsController::instance()
{
    static auto *accounts = new AccountsController(QCoreApplication::instance());
    return accounts;
}

AccountsController *AccountsController::create(QQmlEngine *, QJSEngine *)
{
    AccountsController *accounts = instance();
    QJSEngine::setObjectOwnership(accounts, QJSEngine::CppOwnership);
    return accounts;
}

void AccountsController::setUp(const Setup &setup)
{
    m_setup = setup;
    delete m_manager;
    delete m_api;
    m_manager = nullptr;
    m_api = nullptr;
    if (m_setup.store && m_setup.tokens) {
        m_manager = new AccountManager(*m_setup.tokens, *m_setup.store, m_setup.cache, this);
        m_manager->setContacts(m_setup.contacts);
        connect(m_manager, &AccountManager::connected, this, [this](const Account &account) {
            qCInfo(lcAccounts) << "connected" << account.id;
            const bool current = !m_auth.isNull();
            finish({});
            // Signing in again leaves the list as it was, so nothing else syncs
            // the account with its new token.
            if (current && m_setup.source) {
                m_setup.source->signedInAgain(account);
                m_setup.source->refresh();
            }
        });
        connect(m_manager, &AccountManager::removed, this, [this](const Account &account) {
            qCInfo(lcAccounts) << "removed" << account.id;
            finish({});
        });
        connect(m_manager, &AccountManager::failed, this, &AccountsController::finish);
    }
    if (m_setup.network) {
        m_api = new GoogleCalendarApi(m_setup.network, this);
        if (m_setup.apiBaseUrl.isValid())
            m_api->setBaseUrl(m_setup.apiBaseUrl);
    }
    reload();
}

QVariantList AccountsController::accounts() const
{
    QVariantList list;
    for (const Account &account : m_accounts)
        list << QVariantMap{{u"id"_s, account.id}, {u"provider"_s, account.provider}};
    return list;
}

QString AccountsController::unavailable() const
{
    if (!m_setup.unavailable.isEmpty())
        return m_setup.unavailable;
    if (!m_manager || !m_api)
        return tr("Accounts can be managed in the app only.");
    if (!m_setup.client.isValid())
        return tr("This build has no Google sign-in configured, so accounts cannot be added.");
    return {};
}

void AccountsController::connectGoogle()
{
    signIn({});
}

void AccountsController::reconnect(const QString &id)
{
    signIn(id);
}

void AccountsController::signIn(const QString &hint)
{
    if (busy() || !unavailable().isEmpty())
        return;
    m_error.clear();
    m_granted = false;
    m_auth = new GoogleAuth(m_setup.client, this);
    m_auth->setLoginHint(hint);
    if (m_setup.tokenUrl.isValid())
        m_auth->setEndpoints(m_setup.authUrl, m_setup.tokenUrl);
    connect(m_auth, &GoogleAuth::granted, this, [this] {
        m_granted = true;
        m_status = tr("Saving the account...");
        Q_EMIT stateChanged();
    });
    connect(m_auth, &GoogleAuth::authorizeUrlReady, this, [this](const QUrl &url) {
        m_status = tr("Finish signing in to Google in your browser.");
        Q_EMIT stateChanged();
        if (!QDesktopServices::openUrl(url))
            m_status = tr("Open this address to sign in: %1").arg(url.toString(QUrl::FullyEncoded));
        Q_EMIT stateChanged();
    });
    m_status = tr("Opening your browser...");
    Q_EMIT stateChanged();
    m_manager->connectGoogle(*m_auth, *m_api);
}

void AccountsController::cancel()
{
    if (!signingIn())
        return;
    // Deleted now, not later, so a sign-in finishing in the browser meanwhile
    // reaches nothing and cannot add the account or end the next attempt.
    delete m_auth;
    m_status.clear();
    Q_EMIT stateChanged();
}

void AccountsController::remove(const QString &id)
{
    if (busy() || !m_manager)
        return;
    for (const Account &account : std::as_const(m_accounts)) {
        if (account.id != id)
            continue;
        m_removing = true;
        m_error.clear();
        m_status = tr("Removing %1...").arg(id);
        Q_EMIT stateChanged();
        m_manager->remove(account);
        return;
    }
}

void AccountsController::reload()
{
    QList<Account> accounts;
    if (m_setup.store && !m_setup.store->load(accounts))
        m_error = m_setup.store->errorString();
    accounts.removeIf([](const Account &a) { return a.provider != u"google"; });
    if (accounts != m_accounts) {
        m_accounts = accounts;
        Q_EMIT accountsChanged();
    }
    if (m_setup.source)
        m_setup.source->setAccounts(accounts);
}

void AccountsController::finish(const QString &error)
{
    // Only the attempt in progress may finish; a cancelled one has no auth left.
    if (!m_auth && !m_removing)
        return;
    if (m_auth)
        m_auth->deleteLater();
    // Cleared now rather than when the deferred delete runs, so busy() is
    // already false when stateChanged() is emitted below.
    m_auth.clear();
    m_granted = false;
    m_removing = false;
    m_status.clear();
    m_error = error;
    reload();
    Q_EMIT stateChanged();
}

} // namespace callie
