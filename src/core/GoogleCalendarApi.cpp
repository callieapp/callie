#include "callie/GoogleCalendarApi.h"

#include "callie/Logging.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimeZone>

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
    for (const QJsonValue &attendee : item[u"attendees"].toArray()) {
        if (attendee[u"self"].toBool()) {
            event.responseStatus = attendee[u"responseStatus"].toString();
            break;
        }
    }
    return event;
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

    QNetworkReply *reply = m_network->get(request);
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
