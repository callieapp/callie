#include "callie/OAuthReplyHandler.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>

namespace callie {

void OAuthReplyHandler::networkReplyFinished(QNetworkReply *reply)
{
    m_refusal.clear();
    if (reply->error() != QNetworkReply::NoError) {
        // Peeked, so Qt still reads the reply as it would.
        const QJsonObject body =
            QJsonDocument::fromJson(reply->peek(reply->bytesAvailable())).object();
        const QString error = body[u"error"].toString();
        // Microsoft's descriptions carry a trace and time on later lines.
        const QString description = body[u"error_description"].toString().section(u'\n', 0, 0);
        if (!error.isEmpty())
            m_refusal = description.isEmpty() ? error : error + u": " + description.trimmed();
    }
    QOAuthHttpServerReplyHandler::networkReplyFinished(reply);
}

} // namespace callie
