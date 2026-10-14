#pragma once

#include "callie/Account.h"
#include "callie/GoogleAuth.h"
#include "callie/GoogleClientConfig.h"
#include "callie/MicrosoftAuth.h"

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantList>

class QNetworkAccessManager;

namespace callie {

class AccountManager;
class AccountStore;
class CalDavClient;
class CalDavSource;
class GraphApi;
class MicrosoftSource;
class ContactBook;
class GoogleCache;
class GoogleCalendarApi;
class GoogleSource;
class TokenStore;

/// The accounts section of settings: lists the connected Google accounts,
/// connects another through the browser, and removes one. main.cpp sets it
/// up; without that, as in the gallery, it lists nothing and cannot connect.
class AccountsController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Accounts)
    QML_SINGLETON
    /// {id, provider, server, user} for each connected account.
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
    /// Why accounts cannot be connected here, or empty when they can.
    Q_PROPERTY(QString unavailable READ unavailable CONSTANT)
    /// CalDAV accounts can be connected here; they need no Google sign-in.
    Q_PROPERTY(bool calDavReady READ calDavReady CONSTANT)
    /// Microsoft accounts can be connected: the build or environment names the app.
    Q_PROPERTY(bool microsoftReady READ microsoftReady CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    /// A sign-in is waiting on the browser, which cancel() can stop. Once Google
    /// grants access the account is being saved, and like a removal that runs on.
    Q_PROPERTY(bool signingIn READ signingIn NOTIFY stateChanged)
    /// What is happening now, such as waiting for the browser.
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)

public:
    struct Setup
    {
        AccountStore *store = nullptr;
        TokenStore *tokens = nullptr;
        GoogleCache *cache = nullptr;
        GoogleSource *source = nullptr;
        QNetworkAccessManager *network = nullptr;
        GoogleClientConfig client;
        /// Set when accounts must not change, such as while showing sample data.
        QString unavailable;
        /// Google's endpoints; tests point them at fakes.
        QUrl authUrl = {};
        QUrl tokenUrl = {};
        QUrl apiBaseUrl = {};
        /// Loses an account's contacts when the account is removed.
        ContactBook *contacts = nullptr;
        /// Syncs the CalDAV accounts, and loses their calendars when removed.
        CalDavSource *caldav = nullptr;
        /// Signs in to Microsoft accounts and syncs them, through these
        /// endpoints, which tests point at fakes.
        MicrosoftClientConfig microsoftClient = {};
        MicrosoftSource *microsoft = nullptr;
        QUrl microsoftAuthUrl = {};
        QUrl microsoftTokenUrl = {};
        QUrl graphBaseUrl = {};
    };

    static AccountsController *instance();
    static AccountsController *create(QQmlEngine *, QJSEngine *);

    void setUp(const Setup &setup);

    [[nodiscard]] QVariantList accounts() const;
    [[nodiscard]] QString unavailable() const;
    [[nodiscard]] bool busy() const
    {
        return m_auth != nullptr || m_microsoftAuth != nullptr || m_removing || m_checking;
    }
    [[nodiscard]] bool calDavReady() const;
    [[nodiscard]] bool microsoftReady() const;
    [[nodiscard]] bool signingIn() const
    {
        return (m_auth != nullptr || m_microsoftAuth != nullptr) && !m_granted;
    }
    [[nodiscard]] QString status() const { return m_status; }
    [[nodiscard]] QString error() const { return m_error; }

    /// Opens the browser to sign in with Google; the account appears once done.
    Q_INVOKABLE void connectGoogle();
    /// Opens the browser to sign in with Microsoft: Outlook.com or Microsoft 365.
    Q_INVOKABLE void connectMicrosoft();
    /// Signs an account in again, with that address picked in the browser, so
    /// it can grant permissions added since it first signed in.
    Q_INVOKABLE void reconnect(const QString &id);
    Q_INVOKABLE void cancel();
    /// Signs in to a CalDAV server: `server` may leave out https://.
    Q_INVOKABLE void connectCalDav(const QString &server, const QString &user,
                                   const QString &password);
    /// {id, name, server, help} for the services set up for, and "other".
    Q_INVOKABLE static QVariantList calDavPresets();
    /// Forgets an account's sign-in, cached events and listing.
    Q_INVOKABLE void remove(const QString &provider, const QString &id);
    /// Forgets an account's cached calendars and events and syncs them afresh.
    Q_INVOKABLE void resync(const QString &provider, const QString &id);

Q_SIGNALS:
    void accountsChanged();
    void stateChanged();

private:
    void signIn(const QString &hint);
    void signInToMicrosoft(const QString &hint);
    explicit AccountsController(QObject *parent);
    void reload();
    void finish(const QString &error);

    Setup m_setup;
    AccountManager *m_manager = nullptr;
    GoogleCalendarApi *m_api = nullptr;
    QPointer<GoogleAuth> m_auth;
    QPointer<MicrosoftAuth> m_microsoftAuth;
    GraphApi *m_graph = nullptr;
    /// Google said yes; the account is being stored and can no longer be stopped.
    bool m_granted = false;
    bool m_removing = false;
    /// A CalDAV server is being asked whether the password works.
    bool m_checking = false;
    CalDavClient *m_checker = nullptr;
    QList<Account> m_accounts;
    QString m_status;
    QString m_error;
};

} // namespace callie
