#include "callie/GoogleSource.h"

#include "callie/GoogleCache.h"
#include "callie/GoogleRecurrence.h"
#include "callie/GoogleSync.h"
#include "callie/Logging.h"

#include <QPointer>
#include <QPromise>

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

QList<CalendarInfo> readCalendars(GoogleCache &cache, const QList<Account> &accounts)
{
    QList<CalendarInfo> result;
    for (const Account &account : accounts) {
        for (const GoogleCalendar &calendar : cache.calendars(account)) {
            result.append(CalendarInfo{
                .id = calendarKey(account, calendar.id),
                .displayName = calendar.summary,
                .color = QColor::fromString(calendar.color),
                .writable = canWrite(calendar.accessRole),
                // Google's own "show in list" choice, until Callie has its own.
                .enabled = calendar.selected,
                .account = account.id,
            });
        }
    }
    return result;
}

QList<Event> readEvents(GoogleCache &cache, const QList<Account> &accounts, const QDateTime &from,
                        const QDateTime &to, const QTimeZone &tz)
{
    QList<Event> result;
    for (const Account &account : accounts) {
        for (const GoogleCalendar &calendar : cache.calendars(account)) {
            if (!calendar.selected)
                continue;
            const QColor color = QColor::fromString(calendar.color);
            QList<GoogleEvent> stored = cache.events(account, calendar.id, from, to);
            for (GoogleEvent &event : stored) {
                if (event.remindersUseDefault)
                    event.reminders = calendar.defaultReminders;
            }
            const QList<Event> events = expandGoogleEvents(stored, from, to, tz);
            const bool writable = canWrite(calendar.accessRole);
            for (Event event : events) {
                event.calendarId = calendarKey(account, calendar.id);
                event.canEdit = event.canEdit && writable;
                event.canRespond = event.canRespond && writable;
                event.color = color;
                event.start = event.start.toTimeZone(tz);
                event.end = event.end.toTimeZone(tz);
                result.append(event);
            }
        }
    }
    return result;
}

} // namespace

GoogleSource::GoogleSource(GoogleCache &cache, QList<Account> accounts, QObject *parent)
    : CalendarSource(parent), m_cache(cache), m_accounts(std::move(accounts))
{
    m_readers.setMaxThreadCount(1);
    m_changes.setSingleShot(true);
    m_changes.setInterval(250);
    connect(&m_changes, &QTimer::timeout, this, &CalendarSource::changed);

    loadStatus();
}

void GoogleSource::loadStatus()
{
    m_lastSynced = {};
    m_lastError.clear();
    // From what the last run recorded, so the title bar is right before the
    // first sync of this run finishes.
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

void GoogleSource::setAccounts(QList<Account> accounts)
{
    if (m_accounts == accounts)
        return;
    m_accounts = std::move(accounts);
    // A removed account's errors and sync time go with it; a new one starts unsynced.
    loadStatus();
    Q_EMIT statusChanged();
    Q_EMIT changed();
    if (m_sync && !m_accounts.isEmpty())
        refresh();
}

QList<CalendarInfo> GoogleSource::calendars() const
{
    return readCalendars(m_cache, m_accounts);
}

QList<Event> GoogleSource::eventsBetween(const QDateTime &from, const QDateTime &to,
                                         const QTimeZone &tz) const
{
    return readEvents(m_cache, m_accounts, from, to, tz);
}

QFuture<SourceSnapshot> GoogleSource::load(const QDateTime &from, const QDateTime &to,
                                           const QTimeZone &tz) const
{
    auto promise = std::make_shared<QPromise<SourceSnapshot>>();
    QFuture<SourceSnapshot> future = promise->future();
    promise->start();
    m_readers.start([path = m_cache.path(), accounts = m_accounts, from, to, tz, promise] {
        GoogleCache reader(path);
        SourceSnapshot snapshot;
        if (reader.openForReading()) {
            snapshot.calendars = readCalendars(reader, accounts);
            snapshot.events = readEvents(reader, accounts, from, to, tz);
        } else {
            qCWarning(lcSync) << "could not read the cache:" << reader.errorString();
        }
        promise->addResult(snapshot);
        promise->finish();
    });
    return future;
}

std::optional<std::pair<Account, QString>> GoogleSource::splitCalendarId(const QString &id) const
{
    for (const Account &account : m_accounts) {
        const QString prefix = calendarKey(account, {});
        if (id.startsWith(prefix))
            return std::pair(account, id.mid(prefix.size()));
    }
    return std::nullopt;
}

GoogleSync::Target GoogleSource::target(const QString &calendarId, const Event &event,
                                        bool wholeSeries)
{
    GoogleSync::Target target;
    target.calendarId = calendarId;
    target.seriesId = event.seriesId;
    target.eventId = wholeSeries && !event.seriesId.isEmpty() ? event.seriesId : event.eventId;
    if (event.allDay)
        target.originalStart.date = event.recurrenceId.date();
    else
        target.originalStart.dateTime = event.recurrenceId;
    return target;
}

void GoogleSource::createEvent(const EventDraft &draft, Created done)
{
    const auto calendar = splitCalendarId(draft.calendarId);
    if (!m_sync || !calendar) {
        done(m_sync ? tr("That calendar is not in any connected account.")
                    : tr("Callie is not connected to Google right now."));
        return;
    }
    m_sync->createEvent(calendar->first, calendar->second, draft, std::move(done));
}

void GoogleSource::respond(const Event &event, const QString &status, bool wholeSeries,
                           Created done)
{
    const auto calendar = splitCalendarId(event.calendarId);
    if (!m_sync || !calendar) {
        done(tr("Callie is not connected to that event's account right now."));
        return;
    }
    m_sync->respond(calendar->first, target(calendar->second, event, wholeSeries), status,
                    std::move(done));
}

void GoogleSource::deleteEvent(const Event &event, bool wholeSeries, Created done)
{
    const auto calendar = splitCalendarId(event.calendarId);
    if (!m_sync || !calendar) {
        done(tr("Callie is not connected to that event's account right now."));
        return;
    }
    m_sync->remove(calendar->first, target(calendar->second, event, wholeSeries), std::move(done));
}

QVariantList GoogleSource::syncReport() const
{
    QVariantList report;
    for (const Account &account : m_accounts) {
        const SyncState state = m_cache.accountState(account);
        QStringList problems;
        for (const GoogleCalendar &calendar : m_cache.calendars(account)) {
            const QString error = m_cache.calendarState(account, calendar.id).lastError;
            if (!error.isEmpty())
                problems << QStringLiteral("%1: %2").arg(calendar.summary, error);
        }
        report << QVariantMap{{QStringLiteral("account"), account.id},
                              {QStringLiteral("lastSynced"), state.lastSynced},
                              {QStringLiteral("error"), state.lastError},
                              {QStringLiteral("problems"), problems}};
    }
    return report;
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
