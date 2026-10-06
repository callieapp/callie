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
{
    m_changes.setSingleShot(true);
    m_changes.setInterval(250);
    connect(&m_changes, &QTimer::timeout, this, &CalendarSource::changed);

    // Start from what the last run recorded, so the title bar is right before
    // the first sync of this run finishes.
    for (const Account &account : std::as_const(m_accounts)) {
        const SyncState state = m_cache.accountState(account);
        if (state.lastSynced > m_lastSynced)
            m_lastSynced = state.lastSynced;
        QStringList errors;
        if (!state.lastError.isEmpty())
            errors.append(state.lastError);
        // A run can fail per calendar while the account itself is fine.
        for (const GoogleCalendar &calendar : m_cache.calendars(account)) {
            const QString error = m_cache.calendarState(account, calendar.id).lastError;
            if (!error.isEmpty())
                errors.append(QStringLiteral("%1: %2").arg(calendar.summary, error));
        }
        for (const QString &error : std::as_const(errors))
            m_lastError += (m_lastError.isEmpty() ? QString() : QStringLiteral("\n")) +
                           QStringLiteral("%1: %2").arg(account.id, error);
    }
}

void GoogleSource::setSync(GoogleSync *sync)
{
    if (m_sync)
        disconnect(m_sync, nullptr, this, nullptr);
    m_sync = sync;
    if (m_sync)
        connect(m_sync, &GoogleSync::changed, this, [this] {
            if (!m_changes.isActive())
                m_changes.start();
        });
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
                expandGoogleEvents(m_cache.events(account, calendar.id, from, to), from, to, tz);
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

void GoogleSource::flushChanges()
{
    if (!m_changes.isActive())
        return;
    m_changes.stop();
    Q_EMIT changed();
}

void GoogleSource::refresh()
{
    if (!m_sync) {
        Q_EMIT changed();
        return;
    }
    // GoogleSync joins a sync already running, so overlapping refreshes are cheap.
    if (m_pending == 0)
        m_runErrors.clear();
    m_pending += int(m_accounts.size());
    Q_EMIT statusChanged();
    const QPointer<GoogleSource> self(this);
    for (const Account &account : std::as_const(m_accounts)) {
        m_sync->sync(account, [this, self, account](const QStringList &errors) {
            if (!self)
                return;
            // Overlapping refreshes share one run, so each reports the same errors.
            for (const QString &error : errors) {
                const QString message = QStringLiteral("%1: %2").arg(account.id, error);
                if (m_runErrors.contains(message))
                    continue;
                m_runErrors.append(message);
                Q_EMIT errorOccurred(message);
            }
            if (--m_pending > 0)
                return;
            flushChanges();
            m_lastError = m_runErrors.join(u'\n');
            if (m_runErrors.isEmpty())
                m_lastSynced = QDateTime::currentDateTimeUtc();
            Q_EMIT statusChanged();
        });
    }
}

} // namespace callie
