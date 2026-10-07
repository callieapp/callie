#include "callie/GoogleSync.h"

#include "callie/GoogleCache.h"
#include "callie/GoogleCalendarApi.h"
#include "callie/GoogleTokenProvider.h"
#include "callie/Logging.h"
#include "callie/QuickAdd.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QPointer>

using namespace Qt::StringLiterals;

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
    /// The account was removed; nothing more is written for it.
    bool forgotten = false;
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
    const QJsonObject event = googleEventJson(draft);
    const QPointer<GoogleSync> self(this);
    withToken(
        account, false,
        [this, self, account, calendarId, event, done](const QString &token, const Retry &retry) {
            m_api.insertEvent(
                token, calendarId, event,
                [this, self, account, calendarId, done, retry](const GoogleEvent &created,
                                                               const GoogleApiError &error) {
                    if (!self)
                        return;
                    if (error.unauthorized()) {
                        retry();
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
        },
        done);
}

void GoogleSync::withToken(const Account &account, bool retried, Call call, const Created &failed)
{
    const QPointer<GoogleSync> self(this);
    m_tokens.accessToken(account, [this, self, account, retried, call,
                                   failed](const QString &token, const QString &error) {
        if (!self)
            return;
        if (!error.isEmpty()) {
            failed(error);
            return;
        }
        const Retry retry = [this, self, account, retried, call, failed] {
            if (!self)
                return;
            if (retried) {
                failed(tr("Google rejected the access token"));
                return;
            }
            m_tokens.invalidate(account);
            withToken(account, true, call, failed);
        };
        call(token, retry);
    });
}

void GoogleSync::respond(const Account &account, const Target &target, const QString &status,
                         Created done)
{
    // An occurrence that was never changed is stored only as its series.
    std::optional<GoogleEvent> stored = m_cache.event(account, target.calendarId, target.eventId);
    if (!stored && !target.seriesId.isEmpty())
        stored = m_cache.event(account, target.calendarId, target.seriesId);
    QJsonArray attendees =
        QJsonDocument::fromJson(stored ? stored->attendees : QByteArray()).array();
    bool invited = false;
    for (QJsonValueRef attendee : attendees) {
        QJsonObject guest = attendee.toObject();
        if (guest[u"self"].toBool()) {
            guest[u"responseStatus"_s] = status;
            attendee = guest;
            invited = true;
        }
    }
    if (!invited) {
        done(tr("You are not a guest of this event."));
        return;
    }
    patch(account, target.calendarId, target.eventId, {{u"attendees"_s, attendees}},
          tr("Google could not save the answer: %1"), std::move(done));
}

void GoogleSync::move(const Account &account, const QString &calendarId, const QString &eventId,
                      const QDateTime &start, const QDateTime &end, Created done)
{
    const auto time = [](const QDateTime &moment) {
        QJsonObject json{{u"dateTime"_s, moment.toString(Qt::ISODate)}};
        if (moment.timeSpec() == Qt::TimeZone)
            json.insert(u"timeZone"_s, QString::fromUtf8(moment.timeZone().id()));
        return json;
    };
    patch(account, calendarId, eventId, {{u"start"_s, time(start)}, {u"end"_s, time(end)}},
          tr("Google could not move the event: %1"), std::move(done));
}

void GoogleSync::patch(const Account &account, const QString &calendarId, const QString &eventId,
                       const QJsonObject &fields, const QString &failure, Created done)
{
    const QPointer<GoogleSync> self(this);
    withToken(
        account, false,
        [this, self, account, calendarId, eventId, fields, failure, done](const QString &token,
                                                                          const Retry &retry) {
            m_api.patchEvent(token, calendarId, eventId, fields,
                             [this, self, account, calendarId, failure, done,
                              retry](const GoogleEvent &changedEvent, const GoogleApiError &error) {
                                 if (!self)
                                     return;
                                 if (error.unauthorized()) {
                                     retry();
                                     return;
                                 }
                                 if (error) {
                                     done(failure.arg(error.message));
                                     return;
                                 }
                                 if (!m_cache.storeEvents(account, calendarId, {changedEvent})) {
                                     done(m_cache.errorString());
                                     return;
                                 }
                                 Q_EMIT changed(account);
                                 done({});
                             });
        },
        done);
}

void GoogleSync::remove(const Account &account, const Target &target, Created done)
{
    const QPointer<GoogleSync> self(this);
    withToken(
        account, false,
        [this, self, account, target, done](const QString &token, const Retry &retry) {
            m_api.deleteEvent(
                token, target.calendarId, target.eventId,
                [this, self, account, target, done, retry](const GoogleApiError &error) {
                    if (!self)
                        return;
                    if (error.unauthorized()) {
                        retry();
                        return;
                    }
                    if (error) {
                        done(tr("Google could not delete the event: %1").arg(error.message));
                        return;
                    }
                    // A cancelled occurrence hides just that one; a cancelled
                    // event or series takes the whole of it away.
                    GoogleEvent gone;
                    gone.id = target.eventId;
                    gone.status = u"cancelled"_s;
                    if (!target.seriesId.isEmpty() && target.eventId != target.seriesId) {
                        gone.recurringEventId = target.seriesId;
                        gone.originalStart = target.originalStart;
                    }
                    if (!m_cache.storeEvents(account, target.calendarId, {gone})) {
                        done(m_cache.errorString());
                        return;
                    }
                    Q_EMIT changed(account);
                    done({});
                });
        },
        done);
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
            if (run->forgotten) {
                finish(run);
                return;
            }
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
                if (run->forgotten) {
                    finish(run);
                    return;
                }
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
            if (run->forgotten) {
                calendarDone(run);
                return;
            }
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

void GoogleSync::forget(const Account &account)
{
    if (const std::shared_ptr<Run> run = m_running.take(keyFor(account)))
        run->forgotten = true;
    m_settingsRead.remove(keyFor(account));
}

void GoogleSync::readSettings(const Account &account)
{
    const QPointer<GoogleSync> self(this);
    withToken(
        account, false,
        [this, self, account](const QString &token, const Retry &retry) {
            m_api.fetchSettings(
                token, [this, self, account, retry](const QHash<QString, QString> &settings,
                                                    const GoogleApiError &error) {
                    if (!self)
                        return;
                    if (error.unauthorized()) {
                        retry();
                        return;
                    }
                    // Tried again after the next sync. An account signed in before
                    // Callie asked to read settings is refused until it signs in
                    // again, which is expected, so that stays quiet.
                    if (error) {
                        if (error.status == 403)
                            qCDebug(lcSync) << "Google settings not readable:" << error.message;
                        else
                            qCInfo(lcSync) << "could not read Google settings:" << error.message;
                        return;
                    }
                    m_settingsRead.insert(keyFor(account));
                    Q_EMIT settingsFound(account, settings);
                });
        },
        [](const QString &) {});
}

void GoogleSync::finish(const std::shared_ptr<Run> &run)
{
    if (run->forgotten) {
        for (const Done &done : std::as_const(run->waiting))
            done({});
        return;
    }
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
    if (run->accountError.isEmpty() && !m_settingsRead.contains(keyFor(run->account)))
        readSettings(run->account);
    for (const Done &done : std::as_const(run->waiting))
        done(run->errors);
}

} // namespace callie
