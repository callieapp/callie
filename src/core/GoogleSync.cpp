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
#include <QTimer>

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

// No answer, a busy or failing server, or too many requests can all pass.
bool worthRetrying(const GoogleApiError &error)
{
    return error.status == 0 || error.status == 408 || error.status == 429 || error.status >= 500;
}

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
{
    connect(&m_tokens, &GoogleTokenProvider::scopesKnown, this, &GoogleSync::scopesKnown);
}

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
                        bool conference, Created done)
{
    const QPointer<GoogleSync> self(this);
    withToken(
        account, false,
        [this, self, account, calendarId, event, conference, done](const QString &token,
                                                                   const Retry &retry) {
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
                    // Its id is taken: an earlier try got there, though its answer
                    // did not, and the next sync brings the event.
                    if (error.status == 409) {
                        Q_EMIT changed(account);
                        done({});
                        return;
                    }
                    if (error) {
                        done({tr("Google could not create the event: %1").arg(error.message),
                              worthRetrying(error)});
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
                },
                conference);
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
        // No token yet: the network, the keyring, or a sign-in to renew,
        // all of which can pass, so a change waits for them.
        if (!error.isEmpty()) {
            failed({error, true});
            return;
        }
        const Retry retry = [this, self, account, retried, call, failed] {
            if (!self)
                return;
            if (retried) {
                failed({tr("Google rejected the access token"), true});
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

void GoogleSync::update(const Account &account, const QString &calendarId, const QString &eventId,
                        const QJsonObject &fields, bool conference, Created done)
{
    patch(account, calendarId, eventId, fields, tr("Google could not change the event: %1"),
          std::move(done), conference);
}

void GoogleSync::patch(const Account &account, const QString &calendarId, const QString &eventId,
                       const QJsonObject &fields, const QString &failure, Created done,
                       bool conference)
{
    const QPointer<GoogleSync> self(this);
    withToken(
        account, false,
        [this, self, account, calendarId, eventId, fields, failure, done,
         conference](const QString &token, const Retry &retry) {
            m_api.patchEvent(
                token, calendarId, eventId, fields,
                [this, self, account, calendarId, failure, done,
                 retry](const GoogleEvent &changedEvent, const GoogleApiError &error) {
                    if (!self)
                        return;
                    if (error.unauthorized()) {
                        retry();
                        return;
                    }
                    if (error) {
                        done({failure.arg(error.message), worthRetrying(error)});
                        return;
                    }
                    if (!m_cache.storeEvents(account, calendarId, {changedEvent})) {
                        done(m_cache.errorString());
                        return;
                    }
                    Q_EMIT changed(account);
                    done({});
                },
                conference);
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
                        done({tr("Google could not delete the event: %1").arg(error.message),
                              worthRetrying(error)});
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
    m_tokens.forget(account);
    m_settingsRead.remove(keyFor(account));
    m_contactsRead.remove(keyFor(account));
    m_contactsReading.remove(keyFor(account));
    m_photoRead.remove(keyFor(account));
    m_photoReading.remove(keyFor(account));
}

void GoogleSync::signedInAgain(const Account &account)
{
    m_tokens.invalidate(account);
    m_settingsRead.remove(keyFor(account));
    m_contactsRead.remove(keyFor(account));
    m_photoRead.remove(keyFor(account));
}

QStringList GoogleSync::missingScopes(const Account &account) const
{
    return m_tokens.missingScopes(account);
}

void GoogleSync::readPhoto(const Account &account)
{
    m_photoReading.insert(keyFor(account));
    const QPointer<GoogleSync> self(this);
    withToken(
        account, false,
        [this, self, account](const QString &token, const Retry &retry) {
            m_api.fetchPhoto(token, [this, self, account, retry](const QUrl &photo,
                                                                 const GoogleApiError &error) {
                if (!self)
                    return;
                if (error.unauthorized()) {
                    retry();
                    return;
                }
                // Nothing for an account removed while its photo was being read.
                if (!m_photoReading.remove(keyFor(account)))
                    return;
                // Refused until the account grants its profile; tried again then.
                if (error) {
                    qCDebug(lcSync) << "profile photo not readable:" << error.message;
                    return;
                }
                m_photoRead.insert(keyFor(account));
                Q_EMIT photoFound(account, photo);
            });
        },
        [](const QString &) {});
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

void GoogleSync::readContacts(const Account &account)
{
    // Read once a run even when some kinds are refused: a personal account has
    // no directory, and one signed in before Callie asked for contacts has none
    // of them until it signs in again. Refused counts as empty; a read that
    // fails for now leaves what was read last time alone.
    const QString key = keyFor(account);
    m_contactsRead.insert(key);
    m_contactsReading.insert(key);
    using People = GoogleCalendarApi::People;
    auto found = std::make_shared<QList<Contact>>();
    auto failed = std::make_shared<bool>(false);
    auto next = std::make_shared<std::function<void(qsizetype)>>();
    const QList<People> kinds = {People::Contacts, People::OtherContacts, People::Directory};
    const QPointer<GoogleSync> self(this);
    *next = [this, self, account, key, kinds, found, failed, next](qsizetype i) {
        if (!self)
            return;
        if (i == kinds.size()) {
            // Nothing for an account removed while its contacts were being read.
            if (m_contactsReading.remove(key)) {
                // Tried again after the next sync, rather than leaving a short list.
                if (*failed)
                    m_contactsRead.remove(key);
                else
                    Q_EMIT contactsFound(account, *found);
            }
            // Freed once it has returned, since it is this very function.
            QTimer::singleShot(0, [next] { *next = nullptr; });
            return;
        }
        // Refused is for good: a permission not granted (403), or a directory
        // asked of an account outside Google Workspace (400).
        const bool directory = kinds.at(i) == People::Directory;
        const auto read = [self, directory, i, found, failed, next](const QList<Contact> &people,
                                                                    const GoogleApiError &error) {
            if (!self)
                return;
            if (error) {
                qCDebug(lcSync) << "people not readable:" << error.message;
                const bool refused = error.status == 403 || (directory && error.status == 400);
                *failed = *failed || !refused;
            }
            found->append(people);
            (*next)(i + 1);
        };
        withToken(
            account, false,
            [this, i, kinds, read](const QString &token, const Retry &retry) {
                m_api.fetchPeople(
                    token, kinds.at(i),
                    [read, retry](const QList<Contact> &people, const GoogleApiError &error) {
                        if (error.unauthorized())
                            retry();
                        else
                            read(people, error);
                    });
            },
            [failed, next, i](const QString &) {
                *failed = true;
                (*next)(i + 1);
            });
    };
    (*next)(0);
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
    if (run->accountError.isEmpty() && !m_contactsRead.contains(keyFor(run->account)))
        readContacts(run->account);
    if (run->accountError.isEmpty() && !m_photoRead.contains(keyFor(run->account)))
        readPhoto(run->account);
    for (const Done &done : std::as_const(run->waiting))
        done(run->errors);
}

} // namespace callie
