#pragma once

#include "Contact.h"
#include "GraphApi.h"
#include "VdirSource.h"

#include <QHash>
#include <QPointer>
#include <QSet>
#include <QTimer>

namespace callie {

class MicrosoftTokenProvider;

/// The calendars of Outlook and Microsoft 365 accounts, each kept as a vdir
/// folder that mirrors the account through Microsoft Graph, as CalDavSource
/// does for CalDAV. A change made in Callie lands in the folder at once and is
/// sent in the background; the account's changes come down on refresh(). When
/// an event changed in both places, Outlook's version is kept.
class MicrosoftSource : public VdirSource
{
    Q_OBJECT

public:
    MicrosoftSource(QString root, GraphApi *graph, MicrosoftTokenProvider *tokens,
                    QObject *parent = nullptr);
    ~MicrosoftSource() override;

    /// `$XDG_DATA_HOME/callie/microsoft`
    [[nodiscard]] static QString defaultRoot();

    /// The accounts, by the address each signs in with.
    void setAccounts(const QStringList &accounts);
    [[nodiscard]] QStringList accounts() const { return m_accounts; }
    /// Deletes an account's copies, as when the account is removed.
    bool forget(const QString &accountId, QString *error);
    /// Deletes an account's copies, changes not sent included, and syncs it afresh.
    bool resync(const QString &accountId, QString *error);

    void refresh() override;
    /// A Teams link is asked of Outlook with the next sync; one cannot be taken away.
    void updateEvent(const Event &event, const EventEdit &edit, EditScope scope,
                     Created done) override;

    [[nodiscard]] bool syncing() const override { return !m_running.isEmpty(); }
    [[nodiscard]] QDateTime lastSynced() const override;
    [[nodiscard]] QString lastError() const override;
    [[nodiscard]] QVariantList syncReport() const override;
    [[nodiscard]] QStringList waitingChanges() const override;
    [[nodiscard]] QList<CalendarInfo> calendars() const override;

Q_SIGNALS:
    /// Every sync under way has finished.
    void synced();
    /// The people an account works with, for guest suggestions.
    void contactsFound(const QString &accountId, const QList<callie::Contact> &contacts);

protected:
    void fileChanged(const Folder &folder, const QString &fileName, bool removed) override;

private:
    /// Where a file's event is in Outlook, and its version there.
    struct Remote
    {
        QString id;
        QString changeKey;
        /// The account organizes it, so can change it for everyone.
        bool organizer = true;
    };
    struct Waiting
    {
        bool removed = false;
        qint64 seq = 0;
    };
    /// What is kept about a calendar beside its folder.
    struct State
    {
        QString calendarId;
        bool writable = true;
        /// How Outlook adds online meetings here; empty for none.
        QString meetings;
        QHash<QString, Remote> files;
        QHash<QString, Waiting> pending;
        /// Files whose events wait for a Teams link.
        QSet<QString> teams;
        qint64 seq = 0;
    };
    struct AccountState
    {
        QDateTime synced;
        QString error;
    };
    using Done = std::function<void(const QString &error)>;
    using Call = std::function<void(const QString &token, const std::function<void()> &retry)>;

    [[nodiscard]] QString accountPath(const QString &accountId) const;
    [[nodiscard]] bool hasAccount(const QString &accountId) const;
    void scan();
    void sync(const QString &accountId);
    void finish(const QString &accountId, const QString &error);
    /// Runs `call` with an access token, asking for a new one once if Graph
    /// turns the first down; `failed` hears when no token comes.
    void withToken(const QString &accountId, Call call, const Done &failed, bool retried = false);
    QStringList updateCalendars(const QString &accountId, const QList<QJsonObject> &calendars);
    void syncFolders(const QString &accountId, const QString &token, QList<Folder> folders,
                     QStringList errors);
    void pull(const QString &token, const Folder &folder, Done done);
    void fetchSeries(const QString &token, const Folder &folder, QStringList ids, Done done);
    void push(const QString &token, const Folder &folder, Done done);
    /// Sends the file's change: made, changed or removed.
    void send(const QString &token, const Folder &folder, const QString &fileName,
              const Waiting &waiting, Done done);
    /// Brings the series `id` down into `fileName`, as Outlook has it now.
    void store(const QString &token, const Folder &folder, const QString &fileName,
               const QString &id, Done done);
    /// Changes the occurrences of the series `id` that differ between `before`
    /// and `after`: patched, deleted or answered.
    void sendOccurrences(const QString &token, const QString &id, const ics::Group &before,
                         const ics::Group &after, bool organizer, const QString &self, Done done);
    void readContacts(const QString &accountId, const QString &token);

    [[nodiscard]] static State readState(const QString &folder);
    static void writeState(const QString &folder, const State &state);
    [[nodiscard]] static AccountState readAccount(const QString &path);
    static void writeAccount(const QString &path, const AccountState &state);
    /// The version of a file as Outlook last had it, to tell what changed here.
    [[nodiscard]] static ics::Group serverCopy(const Folder &folder, const QString &fileName);
    static void keepServerCopy(const Folder &folder, const QString &fileName,
                               const QByteArray &data);

    QString m_root;
    QPointer<GraphApi> m_graph;
    MicrosoftTokenProvider *m_tokens;
    QStringList m_accounts;
    QSet<QString> m_running;
    QSet<QString> m_again;
    /// Accounts whose contacts were read in this run of the app.
    QSet<QString> m_contactsRead;
    QTimer m_soon;
};

} // namespace callie
