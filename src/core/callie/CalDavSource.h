#pragma once

#include "CalDavClient.h"
#include "VdirSource.h"

#include <QHash>
#include <QPointer>
#include <QSet>
#include <QTimer>

class QNetworkAccessManager;

namespace callie {

class TokenStore;

/// The calendars of CalDAV accounts, each kept as a vdir folder that mirrors
/// the server. A change made in Callie lands in the folder at once and is
/// sent in the background, held while the server cannot be reached; the
/// server's changes come down on refresh(). When an event changed in both
/// places, the server's version is kept.
class CalDavSource : public VdirSource
{
    Q_OBJECT

public:
    struct Account
    {
        /// Account::id, as the account list and the keyring know it.
        QString id;
        QUrl server;
        QString user;
    };

    /// The password for each account comes from `tokens` when it syncs.
    CalDavSource(QString root, QNetworkAccessManager *network, TokenStore *tokens,
                 QObject *parent = nullptr);
    ~CalDavSource() override;

    /// `$XDG_DATA_HOME/callie/caldav`
    [[nodiscard]] static QString defaultRoot();

    void setAccounts(const QList<Account> &accounts);
    [[nodiscard]] QList<Account> accounts() const { return m_accounts; }
    /// Deletes an account's copies, as when the account is removed.
    bool forget(const QString &accountId, QString *error);

    /// Sends what is waiting and brings down what changed, for every account.
    void refresh() override;

    [[nodiscard]] bool syncing() const override { return !m_running.isEmpty(); }
    [[nodiscard]] QDateTime lastSynced() const override;
    [[nodiscard]] QString lastError() const override;
    [[nodiscard]] QVariantList syncReport() const override;
    [[nodiscard]] QStringList waitingChanges() const override;

Q_SIGNALS:
    /// Every sync under way has finished.
    void synced();

protected:
    void fileChanged(const Folder &folder, const QString &fileName, bool removed) override;

private:
    /// Where a file is on the server, and its version there.
    struct Remote
    {
        QUrl href;
        QString etag;
    };
    /// A change made in Callie and not sent yet. `seq` tells a change made
    /// while the last one was on its way from that one.
    struct Waiting
    {
        bool removed = false;
        qint64 seq = 0;
    };
    /// What is kept about a calendar beside its folder.
    struct State
    {
        QUrl url;
        bool writable = true;
        /// Gone from the server with changes still to send: kept, read-only,
        /// and no longer synced.
        bool gone = false;
        QString syncToken;
        QHash<QString, Remote> files;
        QHash<QString, Waiting> pending;
        qint64 seq = 0;
    };
    /// What is kept about an account beside its calendars.
    struct AccountState
    {
        QUrl home;
        QStringList addresses;
        QDateTime synced;
        QString error;
    };
    using Done = std::function<void(const QString &error)>;

    [[nodiscard]] QString accountPath(const QString &accountId) const;
    void scan();
    void sync(const Account &account);
    void finish(const Account &account, const QString &error);
    /// Makes the folders match the server's calendars; returns what the user
    /// should hear about, such as a calendar gone with changes not sent.
    QStringList updateCalendars(const Account &account, const QList<DavCalendar> &calendars);
    void syncFolders(const Account &account, QList<Folder> folders, QStringList errors);
    void push(CalDavClient *client, const Folder &folder, Done done);
    void pull(CalDavClient *client, const Folder &folder, Done done);
    /// Brings down `changed` and takes away `removed`. A `whole` listing also
    /// takes away whatever it leaves out.
    void apply(CalDavClient *client, const Folder &folder, const QList<DavItem> &changed,
               QList<QUrl> removed, const QString &token, bool whole, Done done);
    [[nodiscard]] bool hasAccount(const QString &accountId) const;
    /// Stops what an account is doing, as when it goes.
    void stop(const QString &accountId);
    /// Puts the server's copy of `href` back in place of a change it turned down.
    void restore(CalDavClient *client, const Folder &folder, const QString &fileName,
                 const QUrl &href, Done done);

    [[nodiscard]] static State readState(const QString &folder);
    static void writeState(const QString &folder, const State &state);
    [[nodiscard]] static AccountState readAccount(const QString &path);
    static void writeAccount(const QString &path, const AccountState &state);

    QString m_root;
    QPointer<QNetworkAccessManager> m_network;
    TokenStore *m_tokens;
    QList<Account> m_accounts;
    QHash<QString, CalDavClient *> m_clients;
    /// Accounts syncing, and those asked to sync again once they finish.
    QSet<QString> m_running;
    QSet<QString> m_again;
    /// Changes made together go out together.
    QTimer m_soon;
};

} // namespace callie
