#pragma once

#include "Account.h"

#include <QObject>
#include <QUrl>

#include <functional>

namespace callie {

class ContactBook;

class AccountStore;
class CalDavClient;
class CalDavSource;
class GoogleCache;
class GoogleAuth;
class GoogleCalendarApi;
class GraphApi;
class MicrosoftAuth;
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
    /// `caldav`, when given, loses a CalDAV account's calendars when it is removed.
    void setCalDav(CalDavSource *caldav) { m_caldav = caldav; }

    /// Signs in with Google and records the account. The token is stored before
    /// the account is listed, so a keyring failure never leaves an account
    /// without a token. Emits connected or failed once.
    void connectGoogle(GoogleAuth &auth, GoogleCalendarApi &api);

    /// Signs in with Microsoft and records the account under its address, as
    /// connectGoogle does. Emits connected or failed once.
    void connectMicrosoft(MicrosoftAuth &auth, GraphApi &graph);

    /// Checks that the server takes the password and has calendars, then stores
    /// the password and lists the account, as connectGoogle does. For an account
    /// already listed, its password and server change. A name or password in
    /// `given` is dropped. Emits connected or failed once.
    void connectCalDav(CalDavClient &client, const QUrl &given, const QString &user,
                       const QString &password);
    /// The id a CalDAV account has: the user name with the server's host, as
    /// "me@dav.example.com", or "me@example.com (dav.example.com)".
    [[nodiscard]] static QString calDavId(const QUrl &server, const QString &user);

    /// Forgets the token, then cached events or a CalDAV account's copies,
    /// then the account, so a failed step leaves the account listed for
    /// another try. Emits removed or failed once.
    void remove(const Account &account);

Q_SIGNALS:
    void connected(const callie::Account &account);
    void removed(const callie::Account &account);
    void failed(const QString &message);

private:
    /// One sign-in under way: what owns its connections, and the calls that
    /// end it, of which only the first counts.
    struct Attempt
    {
        QObject *context;
        std::function<bool()> finish;
        std::function<void(const QString &)> fail;
    };
    [[nodiscard]] Attempt begin();

    /// Stores `secret` for the account, then lists it, undoing the first if
    /// the second fails; `finish` says whether this outcome is the first.
    void record(const Account &account, const QString &secret, const std::function<bool()> &finish,
                const std::function<void(const QString &)> &fail);

    TokenStore &m_tokens;
    AccountStore &m_store;
    GoogleCache *m_cache;
    ContactBook *m_contacts = nullptr;
    CalDavSource *m_caldav = nullptr;
};

} // namespace callie
