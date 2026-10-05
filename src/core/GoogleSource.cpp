#include "callie/GoogleSource.h"

#include "callie/GoogleCache.h"
#include "callie/GoogleRecurrence.h"
#include "callie/GoogleSync.h"

#include <QPointer>

namespace callie {

namespace {

QString calendarKey(const Account &account, const QString &calendarId)
{
    return account.id + u'/' + calendarId;
}

bool canWrite(const QString &accessRole)
{
    return accessRole == QLatin1String("owner") || accessRole == QLatin1String("writer");
}

} // namespace

GoogleSource::GoogleSource(GoogleCache &cache, QList<Account> accounts, QObject *parent)
    : CalendarSource(parent), m_cache(cache), m_accounts(std::move(accounts))
{}

void GoogleSource::setSync(GoogleSync *sync)
{
    if (m_sync)
        disconnect(m_sync, nullptr, this, nullptr);
    m_sync = sync;
    if (m_sync)
        connect(m_sync, &GoogleSync::changed, this, &CalendarSource::changed);
}

QList<CalendarInfo> GoogleSource::calendars() const
{
    QList<CalendarInfo> result;
    for (const Account &account : m_accounts) {
        for (const GoogleCalendar &calendar : m_cache.calendars(account)) {
            result.append(CalendarInfo{
                .id = calendarKey(account, calendar.id),
                .displayName = calendar.summary,
                .color = QColor::fromString(calendar.color),
                .writable = canWrite(calendar.accessRole),
                // Google's own "show in list" choice, until Callie has its own.
                .enabled = calendar.selected,
            });
        }
    }
    return result;
}

QList<Event> GoogleSource::eventsBetween(const QDateTime &from, const QDateTime &to,
                                         const QTimeZone &tz) const
{
    QList<Event> result;
    for (const Account &account : m_accounts) {
        for (const GoogleCalendar &calendar : m_cache.calendars(account)) {
            if (!calendar.selected)
                continue;
            const QColor color = QColor::fromString(calendar.color);
            const QList<Event> events =
                expandGoogleEvents(m_cache.events(account, calendar.id), from, to, tz);
            for (Event event : events) {
                event.calendarId = calendarKey(account, calendar.id);
                event.color = color;
                event.start = event.start.toTimeZone(tz);
                event.end = event.end.toTimeZone(tz);
                result.append(event);
            }
        }
    }
    return result;
}

void GoogleSource::refresh()
{
    if (!m_sync || m_pending > 0) {
        if (!m_sync)
            Q_EMIT changed();
        return;
    }
    m_pending = int(m_accounts.size());
    const QPointer<GoogleSource> self(this);
    for (const Account &account : std::as_const(m_accounts)) {
        m_sync->sync(account, [this, self, account](const QStringList &errors) {
            if (!self)
                return;
            for (const QString &error : errors)
                Q_EMIT errorOccurred(QStringLiteral("%1: %2").arg(account.id, error));
            --m_pending;
        });
    }
}

} // namespace callie
