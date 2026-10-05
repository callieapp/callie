#pragma once

#include "Account.h"
#include "GoogleCalendarApi.h"

#include <QString>

namespace callie {

/// Google calendars and events stored locally in SQLite, with the sync token
/// that brings each calendar up to date. Everything here can be re-fetched, so
/// a schema change drops the tables and the next sync starts over.
class GoogleCache
{
public:
    explicit GoogleCache(const QString &path);
    ~GoogleCache();

    GoogleCache(const GoogleCache &) = delete;
    GoogleCache &operator=(const GoogleCache &) = delete;

    /// $XDG_CACHE_HOME/callie/google.sqlite
    [[nodiscard]] static QString defaultPath();

    /// Opens or creates the database. Every other call fails until this succeeds.
    bool open();
    [[nodiscard]] QString errorString() const { return m_error; }

    [[nodiscard]] QList<GoogleCalendar> calendars(const Account &account);

    /// Empty when the calendar has never synced, which means a full sync.
    [[nodiscard]] QString syncToken(const Account &account, const QString &calendarId);

    /// Makes the stored list match `calendars`. Calendars no longer listed
    /// lose their events; the rest keep their events and sync tokens.
    bool setCalendars(const Account &account, const QList<GoogleCalendar> &calendars);

    /// Applies one sync's changes and stores its sync token, atomically. A full
    /// sync replaces every event in the calendar.
    bool applyChanges(const Account &account, const QString &calendarId,
                      const GoogleEventChanges &changes, bool full);

    /// Every stored event in a calendar, series and their exceptions included.
    [[nodiscard]] QList<GoogleEvent> events(const Account &account, const QString &calendarId);

    bool removeAccount(const Account &account);

private:
    bool exec(const QString &statement);
    bool fail(const QString &message);

    QString m_path;
    QString m_connection;
    QString m_error;
    bool m_open = false;
};

} // namespace callie
