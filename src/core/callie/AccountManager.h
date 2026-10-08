#pragma once

#include "Account.h"

#include <QObject>

namespace callie {

class ContactBook;

class AccountStore;
class GoogleCache;
class GoogleAuth;
class GoogleCalendarApi;
class TokenStore;

/// Connects and removes accounts. Owns the ordering between the keyring and the
/// account list, so the app and the CLI cannot get it wrong separately.
class AccountManager : public QObject
{
    Q_OBJECT

public:
    /// `cache`, when given, loses an account's events when the account is removed.
    AccountManager(TokenStore &tokens, AccountStore &store, GoogleCache *cache = nullptr,
                   QObject *parent = nullptr);
    /// `contacts`, when given, loses an account's contacts when the account is removed.
    void setContacts(ContactBook *contacts) { m_contacts = contacts; }

    /// Signs in with Google and records the account. The token is stored before
    /// the account is listed, so a keyring failure never leaves an account
    /// without a token. Emits connected or failed once.
    void connectGoogle(GoogleAuth &auth, GoogleCalendarApi &api);

    /// Forgets the token, then cached events, then the account, so a failed
    /// step leaves the account listed for another try. Emits removed or failed once.
    void remove(const Account &account);

Q_SIGNALS:
    void connected(const callie::Account &account);
    void removed(const callie::Account &account);
    void failed(const QString &message);

private:
    TokenStore &m_tokens;
    AccountStore &m_store;
    GoogleCache *m_cache;
    ContactBook *m_contacts = nullptr;
};

} // namespace callie
