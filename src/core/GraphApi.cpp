#include "callie/GraphApi.h"

#include "callie/Logging.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>

using namespace Qt::StringLiterals;

namespace callie {

GraphApi::GraphApi(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), m_network(network)
{}

void GraphApi::fetchMe(const QString &accessToken,
                       std::function<void(const GraphUser &, const GraphError &)> result)
{
    send(accessToken, "GET", u"me?$select=id,mail,userPrincipalName,displayName"_s, {},
         [result](const QJsonObject &body, const GraphError &error) {
             // Personal accounts often have no mail; their sign-in name is the address.
             const QString mail = body[u"mail"].toString();
             const GraphUser user{body[u"id"].toString(),
                                  mail.isEmpty() ? body[u"userPrincipalName"].toString() : mail,
                                  body[u"displayName"].toString()};
             if (!error && user.address.isEmpty())
                 result({}, {200, tr("Microsoft did not say which account signed in")});
             else
                 result(user, error);
         });
}

void GraphApi::send(const QString &accessToken, const QByteArray &method, const QString &path,
                    const QJsonObject &body, Answer answer)
{
    request(accessToken, method, m_baseUrl.resolved(QUrl(path)), body, std::move(answer));
}

void GraphApi::list(const QString &accessToken, const QString &path, Pages pages)
{
    nextPage(accessToken, m_baseUrl.resolved(QUrl(path)), {}, std::move(pages));
}

void GraphApi::nextPage(const QString &accessToken, const QUrl &url, QList<QJsonObject> items,
                        Pages pages)
{
    request(accessToken, "GET", url, {},
            [this, accessToken, items = std::move(items),
             pages = std::move(pages)](const QJsonObject &body, const GraphError &error) mutable {
                if (error) {
                    pages({}, error);
                    return;
                }
                for (const QJsonValue &item : body[u"value"].toArray())
                    items.append(item.toObject());
                const QUrl next(body[u"@odata.nextLink"].toString());
                if (next.isEmpty()) {
                    pages(items, {});
                    return;
                }
                // The token goes only where the calls already go.
                if (next.scheme() != m_baseUrl.scheme() || next.host() != m_baseUrl.host() ||
                    next.port() != m_baseUrl.port()) {
                    pages({}, {200, tr("Microsoft pointed the next page somewhere else")});
                    return;
                }
                nextPage(accessToken, next, std::move(items), std::move(pages));
            });
}

void GraphApi::fetch(const QString &accessToken, const QString &path,
                     std::function<void(const QByteArray &, const GraphError &)> result)
{
    requestRaw(accessToken, "GET", m_baseUrl.resolved(QUrl(path)), {}, std::move(result));
}

void GraphApi::request(const QString &accessToken, const QByteArray &method, const QUrl &url,
                       const QJsonObject &body, Answer answer)
{
    requestRaw(accessToken, method, url, body,
               [answer = std::move(answer)](const QByteArray &data, const GraphError &error) {
                   answer(error ? QJsonObject() : QJsonDocument::fromJson(data).object(), error);
               });
}

void GraphApi::requestRaw(const QString &accessToken, const QByteArray &method, const QUrl &url,
                          const QJsonObject &body, Raw answer)
{
    if (!m_network) {
        answer({}, {0, tr("Callie is not online.")});
        return;
    }
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      u"Callie/"_s + QCoreApplication::applicationVersion());
    // Times come back in UTC; each event says its own zone.
    request.setRawHeader("Prefer", "outlook.timezone=\"UTC\", IdType=\"ImmutableId\"");
    request.setTransferTimeout(30000);
    QByteArray data;
    if (!body.isEmpty()) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
        data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    }
    QNetworkReply *reply = m_network->sendCustomRequest(request, method, data);
    connect(reply, &QNetworkReply::finished, this, [reply, answer = std::move(answer)] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray bytes = reply->readAll();
        if (reply->error() == QNetworkReply::NoError) {
            answer(bytes, {});
            return;
        }
        const QString message =
            QJsonDocument::fromJson(bytes).object()[u"error"][u"message"].toString();
        // The path alone, as in GoogleCalendarApi.
        qCInfo(lcSync).noquote() << "Graph request failed with status" << status << "for"
                                 << reply->url().path() << "-" << message;
        answer({}, {status, message.isEmpty() ? reply->errorString() : message});
    });
}

} // namespace callie
