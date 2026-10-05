#pragma once

#include "Account.h"

#include <QObject>

namespace callie {

class AccountStore;
class GoogleAuth;
class GoogleCalendarApi;
class TokenStore;

/// Connects and removes accounts. Owns the ordering between the keyring and the
/// account list, so the app and the CLI cannot get it wrong separately.
class AccountManager : public QObject
{
    Q_OBJECT

public:
    AccountManager(TokenStore &tokens, AccountStore &store, QObject *parent = nullptr);

    /// Signs in with Google and records the account. The token is stored before
    /// the account is listed, so a keyring failure never leaves an account
    /// without a token. Emits connected or failed once.
    void connectGoogle(GoogleAuth &auth, GoogleCalendarApi &api);

    /// Forgets the token, then the account. Emits removed or failed once.
    void remove(const Account &account);

Q_SIGNALS:
    void connected(const callie::Account &account);
    void removed(const callie::Account &account);
    void failed(const QString &message);

private:
    TokenStore &m_tokens;
    AccountStore &m_store;
};

} // namespace callie
