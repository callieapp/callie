#pragma once

#include "Contact.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QJsonObject;

namespace callie {

struct EventDraft;

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
    /// Minutes before an event that the calendar reminds by default, for events
    /// that use its defaults. Only on-screen ("popup") reminders count.
    QList<int> defaultReminders;
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
    /// The user's own answer as an attendee: "accepted", "declined",
    /// "tentative" or "needsAction". Empty for events without attendees.
    QString responseStatus;
    /// The attendee list exactly as Google sent it, compact JSON, so an answer
    /// can be sent back without losing fields Callie does not use.
    QByteArray attendees;
    /// The user organizes the event, so can change it for everyone.
    bool organizerSelf = false;
    /// Guests may change the event too.
    bool guestsCanModify = false;
    /// The event uses its calendar's default reminders, or else its own below.
    bool remindersUseDefault = true;
    /// Minutes before the event for each of its own on-screen reminders.
    QList<int> reminders;

    /// Deleted events, and cancelled instances of a series, arrive with only
    /// an id and this status.
    [[nodiscard]] bool isCancelled() const { return status == QLatin1String("cancelled"); }
};

struct GoogleEventChanges
{
    QList<GoogleEvent> events;
    QString nextSyncToken;
};

/// Google Calendar API v3, and the People API for the people to invite. Every
/// call takes an access token and follows pagination itself, so a result is
/// always complete.
class GoogleCalendarApi : public QObject
{
    Q_OBJECT

public:
    using IdResult = std::function<void(const QString &id, const GoogleApiError &error)>;
    using CalendarsResult =
        std::function<void(const QList<GoogleCalendar> &calendars, const GoogleApiError &error)>;
    using EventsResult =
        std::function<void(const GoogleEventChanges &changes, const GoogleApiError &error)>;
    using EventResult = std::function<void(const GoogleEvent &event, const GoogleApiError &error)>;
    using SettingsResult =
        std::function<void(const QHash<QString, QString> &settings, const GoogleApiError &error)>;
    using PeopleResult =
        std::function<void(const QList<Contact> &people, const GoogleApiError &error)>;

    /// Where the People API finds people.
    enum class People {
        /// The user's saved contacts.
        Contacts,
        /// People the user has written to or met, but not saved.
        OtherContacts,
        /// Everyone in the user's organization, for a Workspace account.
        Directory,
    };

    explicit GoogleCalendarApi(QNetworkAccessManager *network, QObject *parent = nullptr);

    /// Points requests at a fake server in tests. Must end with a slash.
    void setBaseUrl(const QUrl &url) { m_baseUrl = url; }
    void setPeopleBaseUrl(const QUrl &url) { m_peopleUrl = url; }

    /// The primary calendar's id, which Google sets to the account's email.
    void fetchPrimaryCalendarId(const QString &accessToken, IdResult result);

    /// The calendars in the user's list, hidden ones excluded.
    void fetchCalendars(const QString &accessToken, CalendarsResult result);

    /// The user's Google Calendar settings, such as weekStart, by id.
    void fetchSettings(const QString &accessToken, SettingsResult result);

    /// The people of one kind with an email address, as name and address.
    void fetchPeople(const QString &accessToken, People kind, PeopleResult result);

    /// Every event in a calendar when `syncToken` is empty, otherwise only what
    /// changed since that token was issued, deletions included. Recurring
    /// events come back as series, not expanded instances.
    void fetchEvents(const QString &accessToken, const QString &calendarId,
                     const QString &syncToken, EventsResult result);

    /// Creates an event from googleEventJson() and returns it as Google stored it,
    /// inviting its guests if it has any. With `conference`, the event may ask
    /// for a video call.
    void insertEvent(const QString &accessToken, const QString &calendarId,
                     const QJsonObject &event, EventResult result, bool conference = false);

    /// Changes `fields` of one event or occurrence and tells its guests. With
    /// `conference`, the fields may add or remove its video call.
    void patchEvent(const QString &accessToken, const QString &calendarId, const QString &eventId,
                    const QJsonObject &fields, EventResult result, bool conference = false);

    using DoneResult = std::function<void(const GoogleApiError &error)>;

    /// Deletes an event, a whole series, or one occurrence, telling its guests.
    void deleteEvent(const QString &accessToken, const QString &calendarId, const QString &eventId,
                     DoneResult result);

private:
    using Page = std::function<void(const QJsonObject &body, const GoogleApiError &error)>;

    void get(const QString &accessToken, const QString &path,
             const QList<QPair<QString, QString>> &query, Page page);
    void send(QNetworkReply *reply, Page page);
    [[nodiscard]] QUrl eventUrl(const QString &calendarId, const QString &eventId) const;
    void fetchCalendarPage(const QString &accessToken, const QString &pageToken,
                           QList<GoogleCalendar> calendars, CalendarsResult result);
    void fetchSettingsPage(const QString &accessToken, const QString &pageToken,
                           QHash<QString, QString> settings, SettingsResult result);
    void fetchPeoplePage(const QString &accessToken, People kind, const QString &pageToken,
                         QList<Contact> people, PeopleResult result);
    void fetchEventPage(const QString &accessToken, const QString &calendarId,
                        const QString &syncToken, const QString &pageToken,
                        GoogleEventChanges changes, EventsResult result);

    QNetworkAccessManager *m_network;
    QUrl m_baseUrl{QStringLiteral("https://www.googleapis.com/calendar/v3/")};
    QUrl m_peopleUrl{QStringLiteral("https://people.googleapis.com/v1/")};
};

/// Parses one item of an events list. Exposed for tests.
[[nodiscard]] GoogleEvent parseGoogleEvent(const QJsonObject &item);

/// The request body that creates `draft`: dates for all-day events, otherwise
/// times in the draft's own zone.
[[nodiscard]] QJsonObject googleEventJson(const EventDraft &draft);

} // namespace callie
