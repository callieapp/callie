#pragma once

#include "Account.h"
#include "CalendarSource.h"

#include <QTimer>

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

    [[nodiscard]] QString sourceId() const override { return QStringLiteral("google"); }
    [[nodiscard]] QList<CalendarInfo> calendars() const override;
    [[nodiscard]] QList<Event> eventsBetween(const QDateTime &from, const QDateTime &to,
                                             const QTimeZone &tz) const override;
    void refresh() override;

    [[nodiscard]] bool syncing() const override { return m_pending > 0; }
    [[nodiscard]] QDateTime lastSynced() const override { return m_lastSynced; }
    [[nodiscard]] QString lastError() const override { return m_lastError; }

private:
    void flushChanges();

    GoogleCache &m_cache;
    QList<Account> m_accounts;
    GoogleSync *m_sync = nullptr;
    int m_pending = 0;
    QDateTime m_lastSynced;
    QString m_lastError;
    QStringList m_runErrors;
    /// Each synced calendar reports a change; views reload once per burst.
    QTimer m_changes;
};

} // namespace callie
