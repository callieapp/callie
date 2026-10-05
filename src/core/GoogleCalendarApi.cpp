#include "callie/GoogleCalendarApi.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace callie {

GoogleCalendarApi::GoogleCalendarApi(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), m_network(network)
{}

void GoogleCalendarApi::fetchPrimaryCalendarId(const QString &accessToken, IdResult result)
{
    QNetworkRequest request(
        m_baseUrl.resolved(QUrl(QStringLiteral("users/me/calendarList/primary"))));
    request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, result = std::move(result)] {
        reply->deleteLater();
        const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError) {
            const QString message = body[u"error"].toObject()[u"message"].toString();
            result(QString(), message.isEmpty() ? reply->errorString() : message);
            return;
        }
        const QString id = body[u"id"].toString();
        result(id, id.isEmpty() ? tr("Google returned no calendar id") : QString());
    });
}

} // namespace callie
