#pragma once

#include "FakeHttpServer.h"

#include <QMap>
#include <QRegularExpression>
#include <QStringList>

/// A CalDAV server in memory, on FakeHttpServer: discovery through the
/// well-known address, calendars, sync tokens, multiget, and writes and
/// deletes that check versions. Just enough for Callie's client.
class FakeCalDavServer
{
public:
    struct Item
    {
        QByteArray etag;
        QByteArray data;
    };
    struct Calendar
    {
        QByteArray name;
        QByteArray color;
        bool writable = true;
        QMap<QByteArray, Item> items; // by path
    };

    FakeCalDavServer()
    {
        server.replier = [this](const FakeHttpServer::Request &request) { return answer(request); };
    }

    [[nodiscard]] QUrl url(const QString &path = QStringLiteral("/")) const
    {
        return server.url(path);
    }

    /// A calendar at /dav/calendars/me/<id>/.
    void addCalendar(const QByteArray &id, const QByteArray &name, bool writable = true)
    {
        calendars.insert(calendarPath(id), {name, "#33B679FF", writable, {}});
    }
    static QByteArray calendarPath(const QByteArray &id) { return "/dav/calendars/me/" + id + "/"; }

    /// Puts an event in as if made elsewhere, as at `path`.
    void putElsewhere(const QByteArray &id, const QByteArray &name, const QByteArray &data)
    {
        const QByteArray path = calendarPath(id) + name;
        calendars[calendarPath(id)].items.insert(path, {nextEtag(), data});
        log.append({path, false});
    }
    void removeElsewhere(const QByteArray &id, const QByteArray &name)
    {
        const QByteArray path = calendarPath(id) + name;
        calendars[calendarPath(id)].items.remove(path);
        log.append({path, true});
    }

    FakeHttpServer server;
    QByteArray user = "me";
    QByteArray password = "app-password";
    /// Off for servers that only list versions.
    bool syncTokens = true;
    /// Paths that send requests elsewhere, as Location headers.
    QHash<QByteArray, QByteArray> redirects;
    /// Paths answered without signing in, with a multistatus body.
    QHash<QByteArray, QByteArray> anonymous;
    /// The calendar home as the principal names it, when not the usual path.
    QByteArray homeHref = "/dav/calendars/me/";
    QMap<QByteArray, Calendar> calendars;

private:
    struct Change
    {
        QByteArray path;
        bool removed;
    };

    QByteArray nextEtag() { return "\"" + QByteArray::number(++m_versions) + "\""; }
    QByteArray token() const
    {
        return "https://example.com/sync/" + QByteArray::number(log.size());
    }

    static FakeHttpServer::Reply xml(const QByteArray &body)
    {
        return {207,
                "<?xml version=\"1.0\"?><d:multistatus xmlns:d=\"DAV:\" "
                "xmlns:c=\"urn:ietf:params:xml:ns:caldav\" "
                "xmlns:cs=\"http://calendarserver.org/ns/\" "
                "xmlns:a=\"http://apple.com/ns/ical/\">" +
                    body + "</d:multistatus>",
                {},
                "application/xml; charset=utf-8"};
    }
    static QByteArray response(const QByteArray &href, const QByteArray &props)
    {
        return "<d:response><d:href>" + href + "</d:href><d:propstat><d:prop>" + props +
               "</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>";
    }
    static QByteArray escape(const QByteArray &text)
    {
        return QString::fromUtf8(text).toHtmlEscaped().toUtf8();
    }

    FakeHttpServer::Reply answer(const FakeHttpServer::Request &request)
    {
        if (redirects.contains(request.target))
            return {301, "", {{"Location", redirects.value(request.target)}}, "text/plain"};
        if (anonymous.contains(request.target))
            return xml(anonymous.value(request.target));
        if (request.headers.value("authorization") != "Basic " + (user + ':' + password).toBase64())
            return {401, "no", {{"WWW-Authenticate", "Basic realm=\"test\""}}, "text/plain"};
        const QByteArray path = request.target;
        if (request.method == "PROPFIND" && (path == "/" || path == "/.well-known/caldav"))
            return path == "/"
                       ? FakeHttpServer::Reply{404, "", {}, "text/plain"}
                       : FakeHttpServer::Reply{301, "", {{"Location", "/dav/"}}, "text/plain"};
        if (request.method == "PROPFIND" && path == "/dav/")
            return xml(response(path, "<d:current-user-principal><d:href>/dav/principals/me/"
                                      "</d:href></d:current-user-principal>"));
        if (request.method == "PROPFIND" && path == "/dav/principals/me/")
            return xml(
                response(path, "<c:calendar-home-set><d:href>" + homeHref +
                                   "</d:href></c:calendar-home-set><c:calendar-user-address-set>"
                                   "<d:href>mailto:me@example.com</d:href><d:href>/dav/"
                                   "principals/me/</d:href></c:calendar-user-address-set>"));
        if (request.method == "PROPFIND" && path == "/dav/calendars/me/") {
            QByteArray body = response(path, "<d:resourcetype><d:collection/></d:resourcetype>");
            for (auto it = calendars.cbegin(); it != calendars.cend(); ++it)
                body += response(
                    it.key(), "<d:resourcetype><d:collection/><c:calendar/></d:resourcetype>"
                              "<d:displayname>" +
                                  it->name + "</d:displayname><a:calendar-color>" + it->color +
                                  "</a:calendar-color><c:supported-calendar-component-set>"
                                  "<c:comp name=\"VEVENT\"/></c:supported-calendar-component-set>"
                                  "<d:current-user-privilege-set><d:privilege><d:read/>"
                                  "</d:privilege>" +
                                  (it->writable ? "<d:privilege><d:write/></d:privilege>" : "") +
                                  "</d:current-user-privilege-set><d:sync-token>" + token() +
                                  "</d:sync-token>");
            return xml(body);
        }
        if (calendars.contains(path))
            return collection(request, calendars[path]);
        const QByteArray parent = path.left(path.lastIndexOf('/') + 1);
        if (!calendars.contains(parent))
            return {404, "", {}, "text/plain"};
        Calendar &calendar = calendars[parent];
        const auto found = calendar.items.constFind(path);
        const QByteArray ifMatch = request.headers.value("if-match");
        if ((!ifMatch.isEmpty() && (found == calendar.items.cend() || found->etag != ifMatch)) ||
            (request.headers.value("if-none-match") == "*" && found != calendar.items.cend()))
            return {412, "", {}, "text/plain"};
        if (!calendar.writable)
            return {403, "", {}, "text/plain"};
        if (request.method == "PUT") {
            const QByteArray etag = nextEtag();
            calendar.items.insert(path, {etag, request.body});
            log.append({path, false});
            return {found == calendar.items.cend() ? 201 : 204, "", {{"ETag", etag}}, "text/plain"};
        }
        if (request.method == "DELETE") {
            if (found == calendar.items.cend())
                return {404, "", {}, "text/plain"};
            calendar.items.remove(path);
            log.append({path, true});
            return {204, "", {}, "text/plain"};
        }
        return {405, "", {}, "text/plain"};
    }

    FakeHttpServer::Reply collection(const FakeHttpServer::Request &request,
                                     const Calendar &calendar)
    {
        const QByteArray body = request.body;
        if (request.method == "PROPFIND") {
            QByteArray out = response(request.target, "<d:resourcetype><d:collection/>"
                                                      "<c:calendar/></d:resourcetype>");
            for (auto it = calendar.items.cbegin(); it != calendar.items.cend(); ++it)
                out += response(it.key(), "<d:getetag>" + it->etag + "</d:getetag>");
            return xml(out);
        }
        if (request.method == "REPORT" && body.contains("calendar-multiget")) {
            static const QRegularExpression href(QStringLiteral("<d:href>([^<]*)</d:href>"));
            QByteArray out;
            for (const auto &match : href.globalMatch(QString::fromUtf8(body))) {
                const QByteArray path = match.captured(1).toUtf8();
                const auto item = calendar.items.constFind(path);
                if (item != calendar.items.cend())
                    out += response(path, "<d:getetag>" + item->etag +
                                              "</d:getetag><c:calendar-data>" + escape(item->data) +
                                              "</c:calendar-data>");
            }
            return xml(out);
        }
        if (request.method == "REPORT" && body.contains("sync-collection")) {
            if (!syncTokens)
                return {403, "", {}, "text/plain"};
            // As strict servers do: RFC 6578 allows only depth 0.
            if (request.headers.value("depth") != "0")
                return {400, "", {}, "text/plain"};
            static const QRegularExpression tokenText(
                QStringLiteral("<d:sync-token>([^<]*)</d:sync-token>"));
            const QString given = tokenText.match(QString::fromUtf8(body)).captured(1);
            qsizetype from = 0;
            if (!given.isEmpty()) {
                bool ok = false;
                from = given.section(u'/', -1).toLongLong(&ok);
                if (!ok || from > log.size())
                    return {403, "<d:valid-sync-token/>", {}, "application/xml"};
            }
            QByteArray out;
            QStringList seen;
            for (qsizetype i = log.size() - 1; i >= from; --i) {
                const Change &change = log.at(i);
                if (!change.path.startsWith(request.target) || seen.contains(change.path))
                    continue;
                seen << QString::fromUtf8(change.path);
                const auto item = calendar.items.constFind(change.path);
                out += item == calendar.items.cend()
                           ? "<d:response><d:href>" + change.path +
                                 "</d:href><d:status>HTTP/1.1 404 Not Found</d:status></d:response>"
                           : response(change.path, "<d:getetag>" + item->etag + "</d:getetag>");
            }
            return xml(out + "<d:sync-token>" + token() + "</d:sync-token>");
        }
        return {405, "", {}, "text/plain"};
    }

    QList<Change> log;
    int m_versions = 0;
};
