#pragma once

#include <QOAuthHttpServerReplyHandler>

namespace callie {

/// The loopback reply handler, keeping what the provider said when it turned
/// the token request down, which Qt reports only as a failed request.
class OAuthReplyHandler : public QOAuthHttpServerReplyHandler
{
    Q_OBJECT

public:
    using QOAuthHttpServerReplyHandler::QOAuthHttpServerReplyHandler;

    /// The provider's error and description from its last refusal, such as
    /// "invalid_client: AADSTS7000218: ...", or empty.
    [[nodiscard]] QString lastRefusal() const { return m_refusal; }
    /// The last refusal when it was for this error, else empty.
    [[nodiscard]] QString refusalFor(const QString &error) const
    {
        const bool same = !error.isEmpty() && (m_refusal == error ||
                                               m_refusal.startsWith(error + QStringLiteral(": ")));
        return same ? m_refusal : QString();
    }

    void networkReplyFinished(QNetworkReply *reply) override;

private:
    QString m_refusal;
};

} // namespace callie
