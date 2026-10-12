#include "callie/CalDavClient.h"

#include "callie/Logging.h"

#include <QCoreApplication>
#include <QHash>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QXmlStreamReader>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

const QString kDav = u"DAV:"_s;
const QString kCalDav = u"urn:ietf:params:xml:ns:caldav"_s;
const QString kServer = u"http://calendarserver.org/ns/"_s;
const QString kApple = u"http://apple.com/ns/ical/"_s;

/// How many events one multiget asks for, which servers keep small.
constexpr qsizetype kBatch = 50;
constexpr int kMostRedirects = 5;

QString key(const QString &ns, const QString &name)
{
    return ns + u'|' + name;
}

/// The path of an address as the server compares them, without a trailing slash.
QString pathOf(const QUrl &url)
{
    QString path = url.path(QUrl::FullyDecoded);
    while (path.size() > 1 && path.endsWith(u'/'))
        path.chop(1);
    return path;
}

QByteArray escaped(const QString &text)
{
    return text.toHtmlEscaped().toUtf8();
}

int statusOf(const QString &line)
{
    // "HTTP/1.1 404 Not Found"
    return line.section(u' ', 1, 1).toInt();
}

} // namespace

/// What one <response> of a multistatus says.
struct CalDavClient::Response
{
    /// What a property holds: its text, the addresses in it, and the elements
    /// and name attributes inside it, by namespace and name.
    struct Prop
    {
        QString text;
        QList<QUrl> hrefs;
        QStringList children;
        QStringList names;
    };

    QUrl href;
    int status = 200;
    /// The properties found, leaving out those the server could not give.
    QHash<QString, Prop> props;

    [[nodiscard]] Prop prop(const QString &ns, const QString &name) const
    {
        return props.value(key(ns, name));
    }
    [[nodiscard]] bool has(const QString &ns, const QString &name) const
    {
        return props.contains(key(ns, name));
    }
};

CalDavClient::CalDavClient(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), m_network(network)
{}

void CalDavClient::setCredentials(const QString &user, const QString &password)
{
    m_user = user;
    m_password = password;
}

QList<CalDavClient::Response> CalDavClient::multistatus(const QByteArray &xml, const QUrl &base,
                                                        QString *syncToken)
{
    QList<Response> responses;
    QXmlStreamReader reader(xml);

    // One property's contents, read to its end tag.
    const auto readProp = [&reader, &base]() {
        Response::Prop prop;
        int depth = 1;
        while (depth > 0 && !reader.atEnd()) {
            switch (reader.readNext()) {
            case QXmlStreamReader::StartElement:
                prop.children << key(reader.namespaceUri().toString(), reader.name().toString());
                if (const auto name = reader.attributes().value(u"name"_s); !name.isEmpty())
                    prop.names << name.toString().toUpper();
                if (reader.namespaceUri() == kDav && reader.name() == u"href") {
                    prop.hrefs << base.resolved(QUrl(reader.readElementText().trimmed()));
                    break;
                }
                ++depth;
                break;
            case QXmlStreamReader::EndElement: --depth; break;
            case QXmlStreamReader::Characters: prop.text += reader.text(); break;
            default: break;
            }
        }
        prop.text = prop.text.trimmed();
        return prop;
    };

    while (reader.readNextStartElement()) {
        if (reader.namespaceUri() != kDav || reader.name() != u"multistatus")
            return {};
        while (reader.readNextStartElement()) {
            if (reader.namespaceUri() == kDav && reader.name() == u"sync-token") {
                const QString token = reader.readElementText().trimmed();
                if (syncToken)
                    *syncToken = token;
                continue;
            }
            if (reader.namespaceUri() != kDav || reader.name() != u"response") {
                reader.skipCurrentElement();
                continue;
            }
            Response response;
            while (reader.readNextStartElement()) {
                if (reader.namespaceUri() != kDav) {
                    reader.skipCurrentElement();
                } else if (reader.name() == u"href") {
                    response.href = base.resolved(QUrl(reader.readElementText().trimmed()));
                } else if (reader.name() == u"status") {
                    response.status = statusOf(reader.readElementText().trimmed());
                } else if (reader.name() == u"propstat") {
                    QHash<QString, Response::Prop> found;
                    int status = 200;
                    while (reader.readNextStartElement()) {
                        if (reader.namespaceUri() == kDav && reader.name() == u"status") {
                            status = statusOf(reader.readElementText().trimmed());
                        } else if (reader.namespaceUri() == kDav && reader.name() == u"prop") {
                            while (reader.readNextStartElement()) {
                                const QString name =
                                    key(reader.namespaceUri().toString(), reader.name().toString());
                                found.insert(name, readProp());
                            }
                        } else {
                            reader.skipCurrentElement();
                        }
                    }
                    if (status >= 200 && status < 300)
                        response.props.insert(found);
                } else {
                    reader.skipCurrentElement();
                }
            }
            responses.append(response);
        }
    }
    if (reader.hasError())
        qCInfo(lcSync) << "CalDAV: unreadable answer:" << reader.errorString();
    return responses;
}

DavError CalDavClient::failure(QNetworkReply *reply) const
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 401)
        return {status, tr("The server turned down the name or password. iCloud, Fastmail and "
                           "others need an app password, made in the account's own settings.")};
    if (status == 412)
        return {status, tr("The event was changed elsewhere in the meantime.")};
    if (status == 403)
        return {status, tr("The server does not allow that.")};
    if (status >= 400)
        return {status, tr("The server answered %1 for %2.")
                            .arg(status)
                            .arg(reply->url().toString(QUrl::RemoveUserInfo))};
    return {0, reply->errorString()};
}

void CalDavClient::send(const QByteArray &method, const QUrl &url, const QByteArray &body,
                        const QList<std::pair<QByteArray, QByteArray>> &headers, Answered answered,
                        int redirects)
{
    if (!m_network) {
        answered(nullptr, {0, tr("Callie is not online.")});
        return;
    }
    if (m_trusted.isEmpty())
        m_trusted.insert(url.host());
    const bool local = url.host() == u"localhost" || QHostAddress(url.host()).isLoopback();
    if (!m_user.isEmpty() && url.scheme() != u"https" && !local) {
        answered(nullptr, {0, tr("Callie signs in only over https, so the password cannot be "
                                 "read on the way.")});
        return;
    }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      u"Callie/"_s + QCoreApplication::applicationVersion());
    // Only to hosts the account trusts, so a redirect cannot carry it off.
    if (!m_user.isEmpty() && m_trusted.contains(url.host()))
        request.setRawHeader("Authorization",
                             "Basic " + (m_user + u':' + m_password).toUtf8().toBase64());
    for (const auto &[name, value] : headers)
        request.setRawHeader(name, value);
    if (!body.isEmpty() && !request.hasRawHeader("Content-Type"))
        request.setRawHeader("Content-Type", "application/xml; charset=utf-8");
    QNetworkReply *reply = m_network->sendCustomRequest(request, method, body);
    connect(
        reply, &QNetworkReply::finished, this,
        [this, reply, method, url, body, headers, answered, redirects] {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            // Read raw, since Qt leaves a relative Location out of its parsed header.
            const QUrl location = QUrl::fromEncoded(reply->rawHeader("Location"));
            if ((status == 301 || status == 302 || status == 307 || status == 308) &&
                location.isValid()) {
                const QUrl next = url.resolved(location);
                // Never from https down to http.
                if (redirects >= kMostRedirects ||
                    (url.scheme() == u"https" && next.scheme() != u"https")) {
                    answered(reply, {status, tr("The server sent Callie elsewhere too often.")});
                    return;
                }
                send(method, next, body, headers, answered, redirects + 1);
                return;
            }
            if (reply->error() != QNetworkReply::NoError && status != 207) {
                answered(reply, failure(reply));
                return;
            }
            answered(reply, {});
        });
}

void CalDavClient::discover(const QUrl &server,
                            std::function<void(const DavAccount &, const DavError &)> done)
{
    m_trusted = {server.host()};
    findPrincipal(server, false, std::move(done));
}

void CalDavClient::findPrincipal(const QUrl &url, bool wellKnownTried,
                                 std::function<void(const DavAccount &, const DavError &)> done)
{
    static const QByteArray body =
        R"(<d:propfind xmlns:d="DAV:"><d:prop><d:current-user-principal/></d:prop></d:propfind>)";
    const auto tryWellKnown = [this, url, wellKnownTried, done](const DavError &error) {
        if (wellKnownTried || error.isAuth() || error.status == 0) {
            done({}, error ? error
                           : DavError{404, tr("No CalDAV calendars were found at %1.")
                                               .arg(url.toString(QUrl::RemoveUserInfo))});
            return;
        }
        findPrincipal(url.resolved(QUrl(u"/.well-known/caldav"_s)), true, done);
    };
    send("PROPFIND", url, body, {{"Depth", "0"}},
         [this, url, done, tryWellKnown](QNetworkReply *reply, const DavError &error) {
             if (error) {
                 tryWellKnown(error);
                 return;
             }
             QUrl principal;
             for (const Response &response : multistatus(reply->readAll(), reply->url())) {
                 const QList<QUrl> hrefs = response.prop(kDav, u"current-user-principal"_s).hrefs;
                 if (!hrefs.isEmpty())
                     principal = hrefs.first();
             }
             if (!principal.isValid()) {
                 tryWellKnown({});
                 return;
             }
             // Hosts the server itself names, as iCloud does, take the password too.
             m_trusted.insert(principal.host());
             static const QByteArray homeBody =
                 R"(<d:propfind xmlns:d="DAV:" xmlns:c="urn:ietf:params:xml:ns:caldav"><d:prop>)"
                 R"(<c:calendar-home-set/><c:calendar-user-address-set/></d:prop></d:propfind>)";
             send("PROPFIND", principal, homeBody, {{"Depth", "0"}},
                  [this, principal, done](QNetworkReply *homeReply, const DavError &homeError) {
                      if (homeError) {
                          done({}, homeError);
                          return;
                      }
                      DavAccount account;
                      account.principal = principal;
                      for (const Response &response :
                           multistatus(homeReply->readAll(), homeReply->url())) {
                          const QList<QUrl> homes =
                              response.prop(kCalDav, u"calendar-home-set"_s).hrefs;
                          if (!homes.isEmpty())
                              account.home = homes.first();
                          for (const QUrl &address :
                               response.prop(kCalDav, u"calendar-user-address-set"_s).hrefs) {
                              if (address.scheme() == u"mailto")
                                  account.addresses << address.path();
                          }
                      }
                      m_trusted.insert(account.home.host());
                      if (!account.home.isValid()) {
                          done({}, {404, tr("The server has no calendars for this account.")});
                          return;
                      }
                      calendars(account.home, [account, done](const QList<DavCalendar> &found,
                                                              const DavError &listError) mutable {
                          account.calendars = found;
                          done(account, listError);
                      });
                  });
         });
}

void CalDavClient::calendars(const QUrl &home,
                             std::function<void(const QList<DavCalendar> &, const DavError &)> done)
{
    static const QByteArray body =
        R"(<d:propfind xmlns:d="DAV:" xmlns:c="urn:ietf:params:xml:ns:caldav" )"
        R"(xmlns:cs="http://calendarserver.org/ns/" xmlns:a="http://apple.com/ns/ical/">)"
        R"(<d:prop><d:resourcetype/><d:displayname/><a:calendar-color/>)"
        R"(<c:supported-calendar-component-set/><d:current-user-privilege-set/>)"
        R"(<cs:getctag/><d:sync-token/></d:prop></d:propfind>)";
    send("PROPFIND", home, body, {{"Depth", "1"}},
         [this, done](QNetworkReply *reply, const DavError &error) {
             if (error) {
                 done({}, error);
                 return;
             }
             QList<DavCalendar> found;
             for (const Response &response : multistatus(reply->readAll(), reply->url())) {
                 if (!response.prop(kDav, u"resourcetype"_s)
                          .children.contains(key(kCalDav, u"calendar"_s)))
                     continue;
                 // Calendars of to-dos alone have no events to show.
                 const QStringList kinds =
                     response.prop(kCalDav, u"supported-calendar-component-set"_s).names;
                 if (!kinds.isEmpty() && !kinds.contains(u"VEVENT"_s))
                     continue;
                 const QStringList rights =
                     response.prop(kDav, u"current-user-privilege-set"_s).children;
                 DavCalendar calendar;
                 calendar.url = response.href;
                 calendar.displayName = response.prop(kDav, u"displayname"_s).text;
                 calendar.color =
                     QColor::fromString(response.prop(kApple, u"calendar-color"_s).text.left(7));
                 // Servers that say nothing of rights are taken at their word on writing.
                 calendar.writable = rights.isEmpty() || rights.contains(key(kDav, u"write"_s)) ||
                                     rights.contains(key(kDav, u"write-content"_s)) ||
                                     rights.contains(key(kDav, u"all"_s));
                 calendar.ctag = response.prop(kServer, u"getctag"_s).text;
                 calendar.syncToken = response.prop(kDav, u"sync-token"_s).text;
                 m_trusted.insert(calendar.url.host());
                 found.append(calendar);
             }
             done(found, {});
         });
}

void CalDavClient::changes(const QUrl &calendar, const QString &token,
                           std::function<void(const Changes &, const DavError &)> done)
{
    const QByteArray body = R"(<d:sync-collection xmlns:d="DAV:"><d:sync-token>)" + escaped(token) +
                            R"(</d:sync-token><d:sync-level>1</d:sync-level>)"
                            R"(<d:prop><d:getetag/></d:prop></d:sync-collection>)";
    // RFC 6578 asks for depth 0, the level being in the body.
    send("REPORT", calendar, body, {{"Depth", "0"}},
         [calendar, done](QNetworkReply *reply, const DavError &error) {
             if (error) {
                 done({}, error);
                 return;
             }
             Changes changes;
             for (const Response &response :
                  multistatus(reply->readAll(), reply->url(), &changes.token)) {
                 if (pathOf(response.href) == pathOf(calendar))
                     continue;
                 if (response.status == 404)
                     changes.removed << response.href;
                 else if (response.has(kDav, u"getetag"_s))
                     changes.changed
                         << DavItem{response.href, response.prop(kDav, u"getetag"_s).text};
             }
             done(changes, {});
         });
}

void CalDavClient::list(const QUrl &calendar,
                        std::function<void(const QList<DavItem> &, const DavError &)> done)
{
    static const QByteArray body =
        R"(<d:propfind xmlns:d="DAV:"><d:prop><d:getetag/></d:prop></d:propfind>)";
    send("PROPFIND", calendar, body, {{"Depth", "1"}},
         [calendar, done](QNetworkReply *reply, const DavError &error) {
             if (error) {
                 done({}, error);
                 return;
             }
             QList<DavItem> items;
             for (const Response &response : multistatus(reply->readAll(), reply->url())) {
                 if (pathOf(response.href) != pathOf(calendar) && response.has(kDav, u"getetag"_s))
                     items << DavItem{response.href, response.prop(kDav, u"getetag"_s).text};
             }
             done(items, {});
         });
}

void CalDavClient::fetch(const QUrl &calendar, const QList<QUrl> &hrefs,
                         std::function<void(const QList<DavItem> &, const DavError &)> done)
{
    if (hrefs.isEmpty()) {
        done({}, {});
        return;
    }
    QByteArray body =
        R"(<c:calendar-multiget xmlns:d="DAV:" xmlns:c="urn:ietf:params:xml:ns:caldav">)"
        R"(<d:prop><d:getetag/><c:calendar-data/></d:prop>)";
    for (const QUrl &href : hrefs.first(std::min(kBatch, hrefs.size())))
        body += "<d:href>" + escaped(href.path(QUrl::FullyEncoded)) + "</d:href>";
    body += "</c:calendar-multiget>";
    const QList<QUrl> rest = hrefs.mid(std::min(kBatch, hrefs.size()));
    send("REPORT", calendar, body, {{"Depth", "1"}},
         [this, calendar, rest, done](QNetworkReply *reply, const DavError &error) {
             if (error) {
                 done({}, error);
                 return;
             }
             QList<DavItem> items;
             for (const Response &response : multistatus(reply->readAll(), reply->url())) {
                 if (response.has(kCalDav, u"calendar-data"_s))
                     items << DavItem{response.href, response.prop(kDav, u"getetag"_s).text,
                                      response.prop(kCalDav, u"calendar-data"_s).text.toUtf8()};
             }
             if (rest.isEmpty()) {
                 done(items, {});
                 return;
             }
             fetch(calendar, rest,
                   [items, done](const QList<DavItem> &more, const DavError &moreError) {
                       done(items + more, moreError);
                   });
         });
}

void CalDavClient::put(const QUrl &href, const QByteArray &data, const QString &etag,
                       std::function<void(const QString &, const DavError &)> done)
{
    QList<std::pair<QByteArray, QByteArray>> headers{
        {"Content-Type", "text/calendar; charset=utf-8"}};
    headers << (etag.isEmpty() ? std::pair<QByteArray, QByteArray>{"If-None-Match", "*"}
                               : std::pair<QByteArray, QByteArray>{"If-Match", etag.toUtf8()});
    send("PUT", href, data, headers, [done](QNetworkReply *reply, const DavError &error) {
        done(error ? QString() : QString::fromUtf8(reply->rawHeader("ETag")), error);
    });
}

void CalDavClient::remove(const QUrl &href, const QString &etag,
                          std::function<void(const DavError &)> done)
{
    QList<std::pair<QByteArray, QByteArray>> headers;
    if (!etag.isEmpty())
        headers << std::pair<QByteArray, QByteArray>{"If-Match", etag.toUtf8()};
    send("DELETE", href, {}, headers, [done](QNetworkReply *, const DavError &error) {
        done(error.status == 404 ? DavError{} : error);
    });
}

} // namespace callie
