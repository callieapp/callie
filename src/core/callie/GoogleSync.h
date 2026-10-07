#pragma once

#include "Account.h"
#include "GoogleCalendarApi.h"

#include <QHash>
#include <QObject>
#include <QStringList>

#include <functional>
#include <memory>

class QJsonObject;

namespace callie {

struct EventDraft;

class GoogleCache;
class GoogleCalendarApi;
class GoogleTokenProvider;

/// Brings the cache up to date with one Google account: the calendar list,
/// then every calendar's events, incrementally once a calendar has synced.
class GoogleSync : public QObject
{
    Q_OBJECT

public:
    /// Called once per sync with one message per failure; empty on success.
    /// A calendar that fails does not stop the others.
    using Done = std::function<void(const QStringList &errors)>;

    GoogleSync(GoogleTokenProvider &tokens, GoogleCalendarApi &api, GoogleCache &cache,
               QObject *parent = nullptr);
    ~GoogleSync() override;

    /// Joins the sync already running for `account`, if there is one.
    void sync(const Account &account, Done done);

    /// Stops storing anything for a removed account. A sync still running for
    /// it ends quietly, as if it had nothing to report.
    void forget(const Account &account);

    /// Called once a creation finishes; `error` is empty on success.
    using Created = std::function<void(const QString &error)>;

    /// Creates an event in one of the account's calendars and stores it, so it
    /// shows before the next sync.
    void createEvent(const Account &account, const QString &calendarId, const EventDraft &draft,
                     Created done);

    /// What an answer or a deletion is aimed at: one event, one occurrence of
    /// a series (with `seriesId` and its original start), or a whole series.
    struct Target
    {
        QString calendarId;
        QString eventId;
        QString seriesId;
        GoogleEventTime originalStart;
    };

    /// Answers an invitation: "accepted", "tentative" or "declined".
    void respond(const Account &account, const Target &target, const QString &status, Created done);

    /// Deletes the target and drops it from the cache.
    void remove(const Account &account, const Target &target, Created done);

    /// Gives an event, occurrence or series new times, in the zone they carry.
    void move(const Account &account, const QString &calendarId, const QString &eventId,
              const QDateTime &start, const QDateTime &end, Created done);

Q_SIGNALS:
    /// Emitted after each calendar's changes are stored.
    void changed(const callie::Account &account);

private:
    struct Run;

    void start(const std::shared_ptr<Run> &run);
    void syncCalendar(const std::shared_ptr<Run> &run, const QString &calendarId,
                      const QString &name, bool full);
    void calendarDone(const std::shared_ptr<Run> &run);
    void finish(const std::shared_ptr<Run> &run);
    void record(bool stored);
    /// Patches `fields` into an event and stores what Google returns.
    void patch(const Account &account, const QString &calendarId, const QString &eventId,
               const QJsonObject &fields, const QString &failure, Created done);
    /// Runs `call` with an access token. If Google rejects the token, `call`
    /// asks through `retry`, and the token is refreshed for one more try.
    using Retry = std::function<void()>;
    using Call = std::function<void(const QString &token, const Retry &retry)>;
    void withToken(const Account &account, bool retried, Call call, const Created &failed);

    GoogleTokenProvider &m_tokens;
    GoogleCalendarApi &m_api;
    GoogleCache &m_cache;
    QHash<QString, std::shared_ptr<Run>> m_running;
};

} // namespace callie
