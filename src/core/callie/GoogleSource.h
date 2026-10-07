#pragma once

#include "Account.h"
#include "CalendarSource.h"
#include "GoogleSync.h"

#include <QThreadPool>
#include <QTimer>

#include <optional>
#include <utility>

namespace callie {

class GoogleCache;
class GoogleSync;

/// Every connected Google account's calendars, read from GoogleCache. Answers
/// from the cache alone; refresh() syncs when a GoogleSync is attached.
class GoogleSource : public CalendarSource
{
    Q_OBJECT

public:
    GoogleSource(GoogleCache &cache, QList<Account> accounts, QObject *parent = nullptr);

    /// Without one, refresh() only re-reads the cache.
    void setSync(GoogleSync *sync);

    /// Shows a changed set of accounts, as after connecting or removing one,
    /// and syncs them.
    void setAccounts(QList<Account> accounts);
    [[nodiscard]] QList<Account> accounts() const { return m_accounts; }

    [[nodiscard]] QString sourceId() const override { return QStringLiteral("google"); }
    [[nodiscard]] QList<CalendarInfo> calendars() const override;
    [[nodiscard]] QList<Event> eventsBetween(const QDateTime &from, const QDateTime &to,
                                             const QTimeZone &tz) const override;
    /// Reads through a connection of its own on a background thread.
    [[nodiscard]] QFuture<SourceSnapshot> load(const QDateTime &from, const QDateTime &to,
                                               const QTimeZone &tz) const override;
    void refresh() override;
    void createEvent(const EventDraft &draft, Created done) override;
    void respond(const Event &event, const QString &status, bool wholeSeries,
                 Created done) override;
    void deleteEvent(const Event &event, bool wholeSeries, Created done) override;
    void moveEvent(const Event &event, const QDateTime &start, const QDateTime &end,
                   bool wholeSeries, Created done) override;
    void updateEvent(const Event &event, const EventEdit &edit, EditScope scope,
                     Created done) override;

    [[nodiscard]] bool syncing() const override { return m_pending > 0; }
    [[nodiscard]] QDateTime lastSynced() const override { return m_lastSynced; }
    [[nodiscard]] QString lastError() const override { return m_lastError; }
    [[nodiscard]] QVariantList syncReport() const override;

private:
    void flushChanges();
    /// The last sync time and errors the cache recorded for the accounts.
    void loadStatus();
    /// The account a CalendarInfo::id belongs to, and the calendar's own id.
    [[nodiscard]] std::optional<std::pair<Account, QString>>
    splitCalendarId(const QString &id) const;
    /// What an action on `event` aims at in Google's terms.
    [[nodiscard]] static GoogleSync::Target target(const QString &calendarId, const Event &event,
                                                   bool wholeSeries);

    GoogleCache &m_cache;
    QList<Account> m_accounts;
    GoogleSync *m_sync = nullptr;
    int m_pending = 0;
    QDateTime m_lastSynced;
    QString m_lastError;
    QStringList m_runErrors;
    /// Each synced calendar reports a change; views reload once per burst.
    QTimer m_changes;
    /// One reader at a time, so loads finish in the order they were asked for.
    mutable QThreadPool m_readers;
};

} // namespace callie
