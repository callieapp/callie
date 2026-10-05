#pragma once

#include "Account.h"

#include <QHash>
#include <QObject>
#include <QStringList>

#include <functional>
#include <memory>

namespace callie {

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

    GoogleTokenProvider &m_tokens;
    GoogleCalendarApi &m_api;
    GoogleCache &m_cache;
    QHash<QString, std::shared_ptr<Run>> m_running;
};

} // namespace callie
