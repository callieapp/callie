#include "callie/GoogleSync.h"

#include "callie/GoogleCache.h"
#include "callie/GoogleCalendarApi.h"
#include "callie/GoogleTokenProvider.h"
#include "callie/Logging.h"

#include <QPointer>

namespace callie {

struct GoogleSync::Run
{
    Account account;
    QList<Done> waiting;
    QStringList errors;
    QString accessToken;
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
                run->errors.append(error);
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
                    run->errors.append(tr("could not list calendars: %1").arg(error.message));
                    finish(run);
                    return;
                }
                if (!m_cache.setCalendars(run->account, calendars)) {
                    run->errors.append(m_cache.errorString());
                    finish(run);
                    return;
                }
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
            } else if (!m_cache.applyChanges(run->account, calendarId, changes,
                                             syncToken.isEmpty())) {
                run->errors.append(QStringLiteral("%1: %2").arg(name, m_cache.errorString()));
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
        m_tokens.invalidate(run->account);
        start(run);
        return;
    }
    if (run->unauthorized)
        run->errors.append(tr("Google rejected the access token"));

    m_running.remove(keyFor(run->account));
    for (const Done &done : std::as_const(run->waiting))
        done(run->errors);
}

} // namespace callie
