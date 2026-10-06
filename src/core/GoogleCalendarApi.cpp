#include "callie/GoogleCalendarApi.h"

#include "callie/Logging.h"
#include "callie/QuickAdd.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimeZone>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

QString encoded(const QString &value)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

GoogleEventTime parseTime(const QJsonObject &time)
{
    GoogleEventTime result;
    result.timeZone = time[u"timeZone"].toString();
    const QString date = time[u"date"].toString();
    if (!date.isEmpty()) {
        result.date = QDate::fromString(date, Qt::ISODate);
        return result;
    }
    result.dateTime = QDateTime::fromString(time[u"dateTime"].toString(), Qt::ISODateWithMs);
    // Google sends a fixed offset; the named zone is what keeps a weekly
    // meeting at the same wall-clock time across a DST change.
    const QTimeZone zone(result.timeZone.toUtf8());
    if (result.dateTime.isValid() && zone.isValid())
        result.dateTime = result.dateTime.toTimeZone(zone);
    return result;
}

QUrl conferenceUrl(const QJsonObject &item)
{
    const QJsonArray entryPoints = item[u"conferenceData"][u"entryPoints"].toArray();
    for (const QJsonValue &entry : entryPoints) {
        if (entry[u"entryPointType"].toString() == QLatin1String("video"))
            return QUrl(entry[u"uri"].toString());
    }
    const QString hangout = item[u"hangoutLink"].toString();
    return hangout.isEmpty() ? QUrl() : QUrl(hangout);
}

/// The minutes of each on-screen ("popup") reminder in a reminders list.
QList<int> popupMinutes(const QJsonArray &reminders)
{
    QList<int> minutes;
    for (const QJsonValue &reminder : reminders) {
        if (reminder[u"method"].toString() == u"popup")
            minutes.append(reminder[u"minutes"].toInt());
    }
    return minutes;
}

GoogleCalendar parseCalendar(const QJsonObject &item)
{
    return GoogleCalendar{
        .id = item[u"id"].toString(),
        .summary = item[u"summaryOverride"].toString(item[u"summary"].toString()),
        .timeZone = item[u"timeZone"].toString(),
        .color = item[u"backgroundColor"].toString(),
        .accessRole = item[u"accessRole"].toString(),
        .primary = item[u"primary"].toBool(),
        .selected = item[u"selected"].toBool(),
        .defaultReminders = popupMinutes(item[u"defaultReminders"].toArray()),
    };
}

} // namespace

GoogleEvent parseGoogleEvent(const QJsonObject &item)
{
    GoogleEvent event;
    event.id = item[u"id"].toString();
    event.status = item[u"status"].toString();
    event.summary = item[u"summary"].toString();
    event.description = item[u"description"].toString();
    event.location = item[u"location"].toString();
    event.conferenceUrl = conferenceUrl(item);
    event.start = parseTime(item[u"start"].toObject());
    event.end = parseTime(item[u"end"].toObject());
    for (const QJsonValue &line : item[u"recurrence"].toArray())
        event.recurrence.append(line.toString());
    event.recurringEventId = item[u"recurringEventId"].toString();
    event.originalStart = parseTime(item[u"originalStartTime"].toObject());
    event.updated = QDateTime::fromString(item[u"updated"].toString(), Qt::ISODateWithMs);
    const QJsonArray attendees = item[u"attendees"].toArray();
    if (!attendees.isEmpty())
        event.attendees = QJsonDocument(attendees).toJson(QJsonDocument::Compact);
    event.organizerSelf = item[u"organizer"][u"self"].toBool();
    event.guestsCanModify = item[u"guestsCanModify"].toBool();
    const QJsonObject reminders = item[u"reminders"].toObject();
    event.remindersUseDefault = reminders[u"useDefault"].toBool(true);
    event.reminders = popupMinutes(reminders[u"overrides"].toArray());
    for (const QJsonValue &attendee : attendees) {
        if (attendee[u"self"].toBool()) {
            event.responseStatus = attendee[u"responseStatus"].toString();
            break;
        }
    }
    return event;
}

/// The address of one event, with guests told about any change made to it.
QUrl GoogleCalendarApi::eventUrl(const QString &calendarId, const QString &eventId) const
{
    QUrl url = m_baseUrl.resolved(
        QUrl(QStringLiteral("calendars/%1/events/%2").arg(encoded(calendarId), encoded(eventId)),
             QUrl::StrictMode));
    url.setQuery(QStringLiteral("sendUpdates=all"));
    return url;
}

QJsonObject googleEventJson(const EventDraft &draft)
{
    const auto time = [&draft](const QDateTime &moment) {
        if (draft.allDay)
            return QJsonObject{{u"date"_s, moment.date().toString(Qt::ISODate)}};
        QJsonObject json{{u"dateTime"_s, moment.toString(Qt::ISODate)}};
        if (moment.timeSpec() == Qt::TimeZone)
            json.insert(u"timeZone"_s, QString::fromUtf8(moment.timeZone().id()));
        return json;
    };
    QJsonObject json{{u"summary"_s, draft.summary},
                     {u"start"_s, time(draft.start)},
                     {u"end"_s, time(draft.end)}};
    if (!draft.location.isEmpty())
        json.insert(u"location"_s, draft.location);
    return json;
}

GoogleCalendarApi::GoogleCalendarApi(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), m_network(network)
{}

void GoogleCalendarApi::get(const QString &accessToken, const QString &path,
                            const QList<QPair<QString, QString>> &query, Page page)
{
    // Built by hand because QUrlQuery leaves '+' alone, which Google reads as
    // a space, and page and sync tokens can contain it.
    QStringList pairs;
    for (const auto &[key, value] : query)
        pairs.append(key + u'=' + encoded(value));
    QUrl url = m_baseUrl.resolved(QUrl(path, QUrl::StrictMode));
    url.setQuery(pairs.join(u'&'), QUrl::StrictMode);

    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());

    send(m_network->get(request), std::move(page));
}

void GoogleCalendarApi::send(QNetworkReply *reply, Page page)
{
    connect(reply, &QNetworkReply::finished, this, [reply, page = std::move(page)] {
        reply->deleteLater();
        const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() == QNetworkReply::NoError) {
            page(body, {});
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString message = body[u"error"][u"message"].toString();
        qCInfo(lcSync) << "Google API request failed with status" << status;
        page({}, {status, message.isEmpty() ? reply->errorString() : message});
    });
}

void GoogleCalendarApi::insertEvent(const QString &accessToken, const QString &calendarId,
                                    const QJsonObject &event, EventResult result)
{
    QNetworkRequest request(m_baseUrl.resolved(
        QUrl(QStringLiteral("calendars/%1/events").arg(encoded(calendarId)), QUrl::StrictMode)));
    request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    send(m_network->post(request, QJsonDocument(event).toJson(QJsonDocument::Compact)),
         [result = std::move(result)](const QJsonObject &body, const GoogleApiError &error) {
             const GoogleEvent created = parseGoogleEvent(body);
             if (!error && created.id.isEmpty())
                 result({}, {200, tr("Google returned no event")});
             else
                 result(created, error);
         });
}

void GoogleCalendarApi::patchEvent(const QString &accessToken, const QString &calendarId,
                                   const QString &eventId, const QJsonObject &fields,
                                   EventResult result)
{
    QNetworkRequest request(eventUrl(calendarId, eventId));
    request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    send(m_network->sendCustomRequest(request, "PATCH",
                                      QJsonDocument(fields).toJson(QJsonDocument::Compact)),
         [result = std::move(result)](const QJsonObject &body, const GoogleApiError &error) {
             result(error ? GoogleEvent() : parseGoogleEvent(body), error);
         });
}

void GoogleCalendarApi::deleteEvent(const QString &accessToken, const QString &calendarId,
                                    const QString &eventId, DoneResult result)
{
    QNetworkRequest request(eventUrl(calendarId, eventId));
    request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
    send(m_network->deleteResource(request),
         [result = std::move(result)](const QJsonObject &, const GoogleApiError &error) {
             // Already gone is as good as deleted.
             result(error.status == 410 ? GoogleApiError() : error);
         });
}

void GoogleCalendarApi::fetchPrimaryCalendarId(const QString &accessToken, IdResult result)
{
    get(accessToken, QStringLiteral("users/me/calendarList/primary"), {},
        [result = std::move(result)](const QJsonObject &body, const GoogleApiError &error) {
            const QString id = body[u"id"].toString();
            if (!error && id.isEmpty())
                result({}, {200, tr("Google returned no calendar id")});
            else
                result(id, error);
        });
}

void GoogleCalendarApi::fetchCalendars(const QString &accessToken, CalendarsResult result)
{
    fetchCalendarPage(accessToken, {}, {}, std::move(result));
}

void GoogleCalendarApi::fetchCalendarPage(const QString &accessToken, const QString &pageToken,
                                          QList<GoogleCalendar> calendars, CalendarsResult result)
{
    QList<QPair<QString, QString>> query{{QStringLiteral("maxResults"), QStringLiteral("250")}};
    if (!pageToken.isEmpty())
        query.append({QStringLiteral("pageToken"), pageToken});

    get(accessToken, QStringLiteral("users/me/calendarList"), query,
        [this, accessToken, calendars = std::move(calendars),
         result = std::move(result)](const QJsonObject &body, const GoogleApiError &error) mutable {
            if (error) {
                result({}, error);
                return;
            }
            for (const QJsonValue &item : body[u"items"].toArray())
                calendars.append(parseCalendar(item.toObject()));
            const QString next = body[u"nextPageToken"].toString();
            if (next.isEmpty())
                result(calendars, {});
            else
                fetchCalendarPage(accessToken, next, std::move(calendars), std::move(result));
        });
}

void GoogleCalendarApi::fetchEvents(const QString &accessToken, const QString &calendarId,
                                    const QString &syncToken, EventsResult result)
{
    fetchEventPage(accessToken, calendarId, syncToken, {}, {}, std::move(result));
}

void GoogleCalendarApi::fetchEventPage(const QString &accessToken, const QString &calendarId,
                                       const QString &syncToken, const QString &pageToken,
                                       GoogleEventChanges changes, EventsResult result)
{
    // Google rejects time bounds alongside a sync token, so a full sync is
    // unbounded too; the next sync token then covers the whole calendar.
    QList<QPair<QString, QString>> query{{QStringLiteral("maxResults"), QStringLiteral("2500")}};
    if (!syncToken.isEmpty())
        query.append({QStringLiteral("syncToken"), syncToken});
    if (!pageToken.isEmpty())
        query.append({QStringLiteral("pageToken"), pageToken});

    get(accessToken, QStringLiteral("calendars/%1/events").arg(encoded(calendarId)), query,
        [this, accessToken, calendarId, syncToken, changes = std::move(changes),
         result = std::move(result)](const QJsonObject &body, const GoogleApiError &error) mutable {
            if (error) {
                result({}, error);
                return;
            }
            for (const QJsonValue &item : body[u"items"].toArray())
                changes.events.append(parseGoogleEvent(item.toObject()));
            const QString next = body[u"nextPageToken"].toString();
            if (!next.isEmpty()) {
                fetchEventPage(accessToken, calendarId, syncToken, next, std::move(changes),
                               std::move(result));
                return;
            }
            changes.nextSyncToken = body[u"nextSyncToken"].toString();
            if (changes.nextSyncToken.isEmpty())
                result({}, {200, tr("Google returned no sync token")});
            else
                result(changes, {});
        });
}

} // namespace callie
