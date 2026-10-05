#pragma once

#include "Account.h"
#include "CalendarSource.h"

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

private:
    GoogleCache &m_cache;
    QList<Account> m_accounts;
    GoogleSync *m_sync = nullptr;
};

} // namespace callie
