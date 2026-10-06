#include "callie/GoogleSync.h"

#include "callie/GoogleCache.h"
#include "callie/GoogleCalendarApi.h"
#include "callie/GoogleTokenProvider.h"
#include "callie/Logging.h"
#include "callie/QuickAdd.h"

#include <QJsonObject>

#include <QPointer>

namespace callie {

struct GoogleSync::Run
{
    Account account;
    QList<Done> waiting;
    QStringList errors;
    QString accessToken;
    /// Why the account as a whole failed: no token, or no calendar list.
    QString accountError;
    int pending = 0;
    bool unauthorized = false;
    bool retried = false;
};

namespace {

QString keyFor(const Account &account)
{
    return account.provider + u'/' + account.id;
}

} // namespace

void GoogleSync::record(bool stored)
{
    // Only the status display loses out, so this is logged rather than reported.
    if (!stored)
        qCWarning(lcSync) << "could not record the sync outcome:" << m_cache.errorString();
}

GoogleSync::GoogleSync(GoogleTokenProvider &tokens, GoogleCalendarApi &api, GoogleCache &cache,
                       QObject *parent)
    : QObject(parent), m_tokens(tokens), m_api(api), m_cache(cache)
{}

GoogleSync::~GoogleSync() = default;

void GoogleSync::sync(const Account &account, Done done)
{
    const QString key = keyFor(account);
    if (const auto running = m_running.value(key)) {
        running->waiting.append(std::move(done));
        return;
    }
    auto run = std::make_shared<Run>();
    run->account = account;
    run->waiting.append(std::move(done));
    m_running.insert(key, run);
    start(run);
}

void GoogleSync::createEvent(const Account &account, const QString &calendarId,
                             const EventDraft &draft, Created done)
{
    insert(account, calendarId, googleEventJson(draft), false, std::move(done));
}

void GoogleSync::insert(const Account &account, const QString &calendarId, const QJsonObject &event,
                        bool retried, Created done)
{
    const QPointer<GoogleSync> self(this);
    m_tokens.accessToken(
        account, [this, self, account, calendarId, event, retried,
                  done = std::move(done)](const QString &token, const QString &error) mutable {
            if (!self)
                return;
            if (!error.isEmpty()) {
                done(error);
                return;
            }
            m_api.insertEvent(
                token, calendarId, event,
                [this, self, account, calendarId, event, retried,
                 done = std::move(done)](const GoogleEvent &created, const GoogleApiError &error) {
                    if (!self)
                        return;
                    if (error.unauthorized() && !retried) {
                        m_tokens.invalidate(account);
                        insert(account, calendarId, event, true, done);
                        return;
                    }
                    if (error) {
                        done(tr("Google could not create the event: %1").arg(error.message));
                        return;
                    }
                    // Stored now so it shows at once, without counting as a sync.
                    if (!m_cache.storeEvents(account, calendarId, {created})) {
                        done(m_cache.errorString());
                        return;
                    }
                    qCInfo(lcSync) << "created an event in" << calendarId;
                    Q_EMIT changed(account);
                    done({});
                });
        });
}

void GoogleSync::start(const std::shared_ptr<Run> &run)
{
    qCInfo(lcSync) << "syncing" << run->account.id;
    // Requests can outlive this object; their callbacks must then do nothing.
    const QPointer<GoogleSync> self(this);
    m_tokens.accessToken(
        run->account, [this, self, run](const QString &token, const QString &error) {
            if (!self)
                return;
            if (!error.isEmpty()) {
                run->accountError = error;
                finish(run);
                return;
            }
            run->accessToken = token;
            m_api.fetchCalendars(token, [this, self, run](const QList<GoogleCalendar> &calendars,
                                                          const GoogleApiError &error) {
                if (!self)
                    return;
                if (error.unauthorized() && !run->retried) {
                    run->unauthorized = true;
                    finish(run);
                    return;
                }
                if (error) {
                    run->accountError = tr("could not list calendars: %1").arg(error.message);
                    finish(run);
                    return;
                }
                if (!m_cache.setCalendars(run->account, calendars)) {
                    run->accountError = m_cache.errorString();
                    finish(run);
                    return;
                }
                // Names, colors and the list itself can change with no event changes.
                Q_EMIT changed(run->account);
                run->pending = int(calendars.size());
                if (run->pending == 0) {
                    finish(run);
                    return;
                }
                for (const GoogleCalendar &calendar : calendars)
                    syncCalendar(run, calendar.id, calendar.summary, false);
            });
        });
}

void GoogleSync::syncCalendar(const std::shared_ptr<Run> &run, const QString &calendarId,
                              const QString &name, bool full)
{
    const QString syncToken = full ? QString() : m_cache.syncToken(run->account, calendarId);
    const QPointer<GoogleSync> self(this);
    m_api.fetchEvents(
        run->accessToken, calendarId, syncToken,
        [this, self, run, calendarId, name, syncToken](const GoogleEventChanges &changes,
                                                       const GoogleApiError &error) {
            if (!self)
                return;
            if (error.syncTokenExpired() && !syncToken.isEmpty()) {
                qCInfo(lcSync) << "sync token expired for" << calendarId << "- syncing in full";
                syncCalendar(run, calendarId, name, true);
                return;
            }
            if (error.unauthorized()) {
                run->unauthorized = true;
            } else if (error) {
                run->errors.append(QStringLiteral("%1: %2").arg(name, error.message));
                record(m_cache.recordCalendarError(run->account, calendarId, error.message));
            } else if (!m_cache.applyChanges(run->account, calendarId, changes,
                                             syncToken.isEmpty())) {
                const QString problem = m_cache.errorString();
                run->errors.append(QStringLiteral("%1: %2").arg(name, problem));
                record(m_cache.recordCalendarError(run->account, calendarId, problem));
            } else {
                qCInfo(lcSync).noquote()
                    << QStringLiteral("%1: %2 changes%3")
                           .arg(calendarId)
                           .arg(changes.events.size())
                           .arg(syncToken.isEmpty() ? QStringLiteral(", full sync") : QString());
                Q_EMIT changed(run->account);
            }
            calendarDone(run);
        });
}

void GoogleSync::calendarDone(const std::shared_ptr<Run> &run)
{
    if (--run->pending == 0)
        finish(run);
}

void GoogleSync::finish(const std::shared_ptr<Run> &run)
{
    // A cached access token can be revoked before it expires. Refresh it and
    // go again once; calendars that already synced only fetch what is new.
    if (run->unauthorized && !run->retried) {
        qCInfo(lcSync) << "access token rejected, refreshing";
        run->unauthorized = false;
        run->retried = true;
        run->errors.clear();
        run->accountError.clear();
        m_tokens.invalidate(run->account);
        start(run);
        return;
    }
    if (run->unauthorized)
        run->accountError = tr("Google rejected the access token");
    if (!run->accountError.isEmpty())
        run->errors.prepend(run->accountError);
    record(m_cache.recordAccountSync(run->account, run->accountError));
    // Info, so the log file keeps every failure while the caller reports them.
    for (const QString &error : std::as_const(run->errors))
        qCInfo(lcSync).noquote() << run->account.id + QStringLiteral(": ") + error;

    m_running.remove(keyFor(run->account));
    for (const Done &done : std::as_const(run->waiting))
        done(run->errors);
}

} // namespace callie
