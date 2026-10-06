#pragma once

#include "callie/Account.h"
#include "callie/GoogleAuth.h"
#include "callie/GoogleClientConfig.h"

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantList>

class QNetworkAccessManager;

namespace callie {

class AccountManager;
class AccountStore;
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
    /// {id, provider} for each connected account.
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
    /// Why accounts cannot be connected here, or empty when they can.
    Q_PROPERTY(QString unavailable READ unavailable CONSTANT)
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
    };

    static AccountsController *instance();
    static AccountsController *create(QQmlEngine *, QJSEngine *);

    void setUp(const Setup &setup);

    [[nodiscard]] QVariantList accounts() const;
    [[nodiscard]] QString unavailable() const;
    [[nodiscard]] bool busy() const { return m_auth != nullptr || m_removing; }
    [[nodiscard]] bool signingIn() const { return m_auth != nullptr && !m_granted; }
    [[nodiscard]] QString status() const { return m_status; }
    [[nodiscard]] QString error() const { return m_error; }

    /// Opens the browser to sign in with Google; the account appears once done.
    Q_INVOKABLE void connectGoogle();
    Q_INVOKABLE void cancel();
    /// Forgets an account's sign-in, cached events and listing.
    Q_INVOKABLE void remove(const QString &id);

Q_SIGNALS:
    void accountsChanged();
    void stateChanged();

private:
    explicit AccountsController(QObject *parent);
    void reload();
    void finish(const QString &error);

    Setup m_setup;
    AccountManager *m_manager = nullptr;
    GoogleCalendarApi *m_api = nullptr;
    QPointer<GoogleAuth> m_auth;
    /// Google said yes; the account is being stored and can no longer be stopped.
    bool m_granted = false;
    bool m_removing = false;
    QList<Account> m_accounts;
    QString m_status;
    QString m_error;
};

} // namespace callie
