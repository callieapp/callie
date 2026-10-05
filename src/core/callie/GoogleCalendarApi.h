#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QJsonObject;

namespace callie {

/// A failed call. `status` is the HTTP status, or 0 when no response arrived.
struct GoogleApiError
{
    int status = 0;
    QString message;

    explicit operator bool() const { return !message.isEmpty(); }

    /// The access token expired or was revoked; refresh it and retry once.
    [[nodiscard]] bool unauthorized() const { return status == 401; }

    /// The sync token is no longer valid; discard local data and sync in full.
    [[nodiscard]] bool syncTokenExpired() const { return status == 410; }
};

struct GoogleCalendar
{
    QString id;
    QString summary;
    QString timeZone;
    QString color;
    QString accessRole;
    bool primary = false;
    bool selected = false;
};

/// Either a date for all-day events or a date-time. `timeZone` is the IANA zone
/// the organizer chose, which recurrence expansion must use.
struct GoogleEventTime
{
    QDate date;
    QDateTime dateTime;
    QString timeZone;

    [[nodiscard]] bool isAllDay() const { return date.isValid(); }
};

/// An event as Google stores it: a single event, a recurring series with its
/// `recurrence` lines, or an exception to a series with `recurringEventId` set.
struct GoogleEvent
{
    QString id;
    QString status;
    QString summary;
    QString description;
    QString location;
    QUrl conferenceUrl;
    GoogleEventTime start;
    GoogleEventTime end;
    QStringList recurrence;
    QString recurringEventId;
    GoogleEventTime originalStart;
    QDateTime updated;

    /// Deleted events, and cancelled instances of a series, arrive with only
    /// an id and this status.
    [[nodiscard]] bool isCancelled() const { return status == QLatin1String("cancelled"); }
};

struct GoogleEventChanges
{
    QList<GoogleEvent> events;
    QString nextSyncToken;
};

/// Google Calendar API v3. Every call takes an access token and follows
/// pagination itself, so a result is always complete.
class GoogleCalendarApi : public QObject
{
    Q_OBJECT

public:
    using IdResult = std::function<void(const QString &id, const GoogleApiError &error)>;
    using CalendarsResult =
        std::function<void(const QList<GoogleCalendar> &calendars, const GoogleApiError &error)>;
    using EventsResult =
        std::function<void(const GoogleEventChanges &changes, const GoogleApiError &error)>;

    explicit GoogleCalendarApi(QNetworkAccessManager *network, QObject *parent = nullptr);

    /// Points requests at a fake server in tests. Must end with a slash.
    void setBaseUrl(const QUrl &url) { m_baseUrl = url; }

    /// The primary calendar's id, which Google sets to the account's email.
    void fetchPrimaryCalendarId(const QString &accessToken, IdResult result);

    /// The calendars in the user's list, hidden ones excluded.
    void fetchCalendars(const QString &accessToken, CalendarsResult result);

    /// Every event in a calendar when `syncToken` is empty, otherwise only what
    /// changed since that token was issued, deletions included. Recurring
    /// events come back as series, not expanded instances.
    void fetchEvents(const QString &accessToken, const QString &calendarId,
                     const QString &syncToken, EventsResult result);

private:
    using Page = std::function<void(const QJsonObject &body, const GoogleApiError &error)>;

    void get(const QString &accessToken, const QString &path,
             const QList<QPair<QString, QString>> &query, Page page);
    void fetchCalendarPage(const QString &accessToken, const QString &pageToken,
                           QList<GoogleCalendar> calendars, CalendarsResult result);
    void fetchEventPage(const QString &accessToken, const QString &calendarId,
                        const QString &syncToken, const QString &pageToken,
                        GoogleEventChanges changes, EventsResult result);

    QNetworkAccessManager *m_network;
    QUrl m_baseUrl{QStringLiteral("https://www.googleapis.com/calendar/v3/")};
};

/// Parses one item of an events list. Exposed for tests.
[[nodiscard]] GoogleEvent parseGoogleEvent(const QJsonObject &item);

} // namespace callie
