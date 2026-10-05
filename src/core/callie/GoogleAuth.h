#pragma once

#include "GoogleClientConfig.h"

#include <QDateTime>
#include <QObject>
#include <QUrl>

class QOAuth2AuthorizationCodeFlow;
class QOAuthHttpServerReplyHandler;

namespace callie {

struct GoogleTokens
{
    QString accessToken;
    QString refreshToken;
    QDateTime expiresAt;
};

/// OAuth 2.0 for Google as an installed app: authorization code flow with PKCE
/// and a loopback redirect. Opening the browser is left to the caller, since
/// the app and the CLI do it differently.
class GoogleAuth : public QObject
{
    Q_OBJECT

public:
    explicit GoogleAuth(const GoogleClientConfig &client, QObject *parent = nullptr);
    ~GoogleAuth() override;

    /// Space-separated scopes requested. Kept narrow to ease Google's review.
    [[nodiscard]] static QString scopes();

    /// Points the flow at a fake server in tests.
    void setEndpoints(const QUrl &authorization, const QUrl &token);

    /// Starts the loopback listener and emits authorizeUrlReady.
    void authorize();

    /// Exchanges a stored refresh token for a new access token.
    void refresh(const QString &refreshToken);

    /// The redirect URI, valid once authorize() has been called.
    [[nodiscard]] QUrl callbackUrl() const;

Q_SIGNALS:
    void authorizeUrlReady(const QUrl &url);
    void granted(const callie::GoogleTokens &tokens);
    void failed(const QString &message);

private:
    void onGranted();
    void fail(const QString &message);

    QOAuth2AuthorizationCodeFlow *m_flow;
    QOAuthHttpServerReplyHandler *m_handler = nullptr;
    QString m_refreshToken;
};

} // namespace callie
