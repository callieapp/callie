#pragma once

#include "FakeHttpServer.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QTimeZone>
#include <QUrlQuery>

/// Microsoft Graph's calendar calls in memory, on FakeHttpServer: calendars,
/// events, daily series with changed and cancelled occurrences, instances,
/// changes, deletes, answers and people. Just enough for MicrosoftSource.
class FakeGraphServer
{
public:
    struct Calendar
    {
        QString name;
        QString color = QStringLiteral("#E86AA6");
        bool canEdit = true;
        QString meetings = QStringLiteral("teamsForBusiness");
    };
    /// A single event or a series master, with its occurrences by original start.
    struct Item
    {
        QString calendar;
        QJsonObject event;
        QMap<QString, QJsonObject> exceptions;
        QStringList cancelled;
    };
    struct Call
    {
        QByteArray method;
        QString path;
        QJsonObject body;
    };

    FakeGraphServer()
    {
        server.replier = [this](const FakeHttpServer::Request &request) { return answer(request); };
    }

    [[nodiscard]] QUrl url() const { return server.url(QStringLiteral("/v1.0/")); }

    void addCalendar(const QString &id, const Calendar &calendar)
    {
        calendars.insert(id, calendar);
    }

    /// An event as Outlook would have it, made elsewhere.
    QString add(const QString &calendar, QJsonObject event)
    {
        const QString id = QStringLiteral("ev%1").arg(++m_ids);
        event.insert(QStringLiteral("id"), id);
        if (!event.contains(QStringLiteral("iCalUId")))
            event.insert(QStringLiteral("iCalUId"), QStringLiteral("uid-") + id);
        event.insert(QStringLiteral("type"), event.contains(QStringLiteral("recurrence"))
                                                 ? QStringLiteral("seriesMaster")
                                                 : QStringLiteral("singleInstance"));
        event.insert(QStringLiteral("changeKey"), nextKey());
        if (!event.contains(QStringLiteral("isOrganizer")))
            event.insert(QStringLiteral("isOrganizer"), true);
        items.insert(id, {calendar, event, {}, {}});
        return id;
    }
    /// Changes an event as if elsewhere.
    void change(const QString &id, const QJsonObject &fields)
    {
        Item &item = items[id];
        for (auto it = fields.begin(); it != fields.end(); ++it)
            item.event.insert(it.key(), it.value());
        item.event.insert(QStringLiteral("changeKey"), nextKey());
    }

    FakeHttpServer server;
    QMap<QString, Calendar> calendars;
    QMap<QString, Item> items;
    QList<Call> calls;
    QJsonArray people;
    /// The account's photo; none set when empty.
    QByteArray photo;
    /// Answers every call with this status, as an Outlook that is down.
    int down = 0;
    /// Answers only the lookups of a series' occurrences with this status.
    int instancesDown = 0;
    /// Turns down every change to an event, and every new one, with this status.
    int refuseChanges = 0;
    /// Runs before a change to an event is answered, as while it is on its way.
    std::function<void()> whileChanging;
    /// Runs before a calendar's events are listed, as while the list is on its way.
    std::function<void()> whileListing;

private:
    static FakeHttpServer::Reply json(const QJsonObject &body, int status = 200)
    {
        return {status, QJsonDocument(body).toJson(QJsonDocument::Compact)};
    }
    static FakeHttpServer::Reply missing()
    {
        return json({{QStringLiteral("error"),
                      QJsonObject{{QStringLiteral("message"), QStringLiteral("not found")}}}},
                    404);
    }
    QString nextKey() { return QStringLiteral("ck%1").arg(++m_keys); }

    static QDateTime start(const QJsonObject &event)
    {
        QDateTime at = QDateTime::fromString(
            event[QStringLiteral("start")][QStringLiteral("dateTime")].toString().left(19),
            Qt::ISODate);
        at.setTimeZone(QTimeZone::UTC);
        return at;
    }
    static QString occurrenceId(const QString &series, const QDateTime &original)
    {
        return series + u'_' + original.toString(QStringLiteral("yyyyMMddTHHmmss"));
    }

    /// The occurrences of a daily series, each with its original start.
    QList<QJsonObject> instances(const QString &series)
    {
        const Item &item = items[series];
        const QJsonObject range =
            item.event[QStringLiteral("recurrence")][QStringLiteral("range")].toObject();
        const int count = range[QStringLiteral("numberOfOccurrences")].toInt(10);
        const QDateTime first = start(item.event);
        QList<QJsonObject> list;
        for (int i = 0; i < count; ++i) {
            const QDateTime original = first.addDays(i);
            const QString key = original.toString(Qt::ISODate);
            if (item.cancelled.contains(original.date().toString(Qt::ISODate)))
                continue;
            QJsonObject occurrence = item.exceptions.value(key, item.event);
            occurrence.insert(QStringLiteral("id"), occurrenceId(series, original));
            occurrence.insert(QStringLiteral("originalStart"), key);
            list << occurrence;
        }
        return list;
    }

    FakeHttpServer::Reply answer(const FakeHttpServer::Request &request)
    {
        const QUrl url(QString::fromUtf8(request.target));
        QString path = url.path(QUrl::FullyDecoded);
        path.remove(0, QStringLiteral("/v1.0/").size());
        const QJsonObject body = QJsonDocument::fromJson(request.body).object();
        calls.append({request.method, path, body});
        if (down)
            return {down, "{}"};
        if (instancesDown && path.endsWith(u"/instances"))
            return {instancesDown, "{}"};
        // Decided before whileChanging runs, which may stop the refusals.
        const int refusal =
            request.method == "PATCH" || request.method == "POST" ? refuseChanges : 0;
        if (whileChanging && (request.method == "PATCH" || request.method == "POST"))
            std::exchange(whileChanging, nullptr)();
        if (refusal)
            return {refusal,
                    R"({"error":{"code":"ErrorAccessDenied","message":"Access is denied."}})"};
        const QStringList parts = path.split(u'/');
        if (request.method == "GET" && path == u"me/calendars") {
            QJsonArray list;
            for (auto it = calendars.cbegin(); it != calendars.cend(); ++it)
                list << QJsonObject{
                    {QStringLiteral("id"), it.key()},
                    {QStringLiteral("name"), it->name},
                    {QStringLiteral("hexColor"), it->color},
                    {QStringLiteral("canEdit"), it->canEdit},
                    {QStringLiteral("defaultOnlineMeetingProvider"), it->meetings},
                    {QStringLiteral("allowedOnlineMeetingProviders"), QJsonArray{it->meetings}}};
            return json({{QStringLiteral("value"), list}});
        }
        if (request.method == "GET" && path == u"me/people")
            return json({{QStringLiteral("value"), people}});
        if (request.method == "GET" && path == u"me/photos/96x96/$value")
            return photo.isEmpty() ? missing()
                                   : FakeHttpServer::Reply{200, photo, {}, "image/jpeg"};
        // me/calendars/{id}/events
        if (parts.size() == 4 && parts[1] == u"calendars" && parts[3] == u"events") {
            if (request.method == "POST") {
                QJsonObject event = body;
                if (event[QStringLiteral("isOnlineMeeting")].toBool())
                    event.insert(
                        QStringLiteral("onlineMeeting"),
                        QJsonObject{{QStringLiteral("joinUrl"),
                                     QStringLiteral("https://teams.microsoft.com/l/new")}});
                const QString id = add(parts[2], event);
                return json(items[id].event, 201);
            }
            if (whileListing)
                std::exchange(whileListing, nullptr)();
            QJsonArray list;
            for (const Item &item : std::as_const(items)) {
                if (item.calendar == parts[2])
                    list << item.event;
            }
            return json({{QStringLiteral("value"), list}});
        }
        if (parts.size() < 3 || parts[1] != u"events")
            return missing();
        // An occurrence's id names its series and its original start.
        QString id = parts[2];
        QString original;
        if (!items.contains(id) && id.contains(u'_')) {
            original = QDateTime::fromString(id.section(u'_', 1), QStringLiteral("yyyyMMddTHHmmss"))
                           .toString(QStringLiteral("yyyy-MM-ddTHH:mm:ssZ"));
            id = id.section(u'_', 0, 0);
        }
        if (!items.contains(id))
            return missing();
        Item &item = items[id];
        if (parts.size() == 4 && parts[3] == u"instances")
            return json({{QStringLiteral("value"), QJsonArray::fromVariantList([&] {
                              QVariantList list;
                              for (const QJsonObject &occurrence : instances(id))
                                  list << occurrence.toVariantMap();
                              return list;
                          }())}});
        if (parts.size() == 4 && request.method == "POST") {
            const QString response = parts[3] == u"accept" ? QStringLiteral("accepted")
                                     : parts[3] == u"decline"
                                         ? QStringLiteral("declined")
                                         : QStringLiteral("tentativelyAccepted");
            QJsonObject &target =
                original.isEmpty()
                    ? item.event
                    : (item.exceptions[original] = item.exceptions.value(original, item.event));
            target.insert(QStringLiteral("responseStatus"),
                          QJsonObject{{QStringLiteral("response"), response}});
            item.event.insert(QStringLiteral("changeKey"), nextKey());
            return {202, ""};
        }
        if (request.method == "DELETE") {
            if (original.isEmpty())
                items.remove(id);
            else {
                item.cancelled << original.left(10);
                item.exceptions.remove(original);
                item.event.insert(QStringLiteral("changeKey"), nextKey());
            }
            return {204, ""};
        }
        if (request.method == "PATCH") {
            if (!original.isEmpty()) {
                QJsonObject occurrence = item.exceptions.value(original, item.event);
                for (auto it = body.begin(); it != body.end(); ++it)
                    occurrence.insert(it.key(), it.value());
                occurrence.remove(QStringLiteral("recurrence"));
                occurrence.insert(QStringLiteral("type"), QStringLiteral("exception"));
                occurrence.insert(QStringLiteral("originalStart"), original);
                item.exceptions.insert(original, occurrence);
                item.event.insert(QStringLiteral("changeKey"), nextKey());
                return json(occurrence);
            }
            QJsonObject fields = body;
            if (fields[QStringLiteral("isOnlineMeeting")].toBool())
                fields.insert(QStringLiteral("onlineMeeting"),
                              QJsonObject{{QStringLiteral("joinUrl"),
                                           QStringLiteral("https://teams.microsoft.com/l/") + id}});
            change(id, fields);
            return json(item.event);
        }
        if (request.method == "GET") {
            QJsonObject event = item.event;
            QJsonArray exceptions;
            for (const QJsonObject &exception : std::as_const(item.exceptions))
                exceptions << exception;
            QJsonArray cancelled;
            for (const QString &day : std::as_const(item.cancelled))
                cancelled << QStringLiteral("OID.%1.%2").arg(id, day);
            event.insert(QStringLiteral("exceptionOccurrences"), exceptions);
            event.insert(QStringLiteral("cancelledOccurrences"), cancelled);
            return json(event);
        }
        return missing();
    }

    int m_ids = 0;
    int m_keys = 0;
};
