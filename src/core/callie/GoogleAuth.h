#pragma once

#include "GoogleClientConfig.h"

#include <QDateTime>
#include <QObject>
#include <QUrl>

#include <chrono>

class QNetworkAccessManager;
class QOAuth2AuthorizationCodeFlow;
class QOAuthHttpServerReplyHandler;
class QTimer;

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

    /// Starts the loopback listener and emits authorizeUrlReady. Can be called
    /// again after an attempt finishes, to retry or add another account.
    void authorize();

    /// Exchanges a stored refresh token for a new access token. A failure
    /// carries Google's own reason, such as a revoked grant.
    void refresh(const QString &refreshToken);

    /// How long authorize() waits for the user to finish in the browser before
    /// failing. Stops as soon as Google answers, so later steps are never cut off.
    void setSignInTimeout(std::chrono::milliseconds timeout);

    /// The redirect URI, valid once authorize() has been called.
    [[nodiscard]] QUrl callbackUrl() const;

Q_SIGNALS:
    void authorizeUrlReady(const QUrl &url);
    void granted(const callie::GoogleTokens &tokens);
    void failed(const QString &message);

private:
    void onGranted();
    void fail(const QString &message);

    GoogleClientConfig m_client;
    QOAuth2AuthorizationCodeFlow *m_flow;
    QNetworkAccessManager *m_network;
    QUrl m_tokenUrl;
    QOAuthHttpServerReplyHandler *m_handler = nullptr;
    QTimer *m_signInTimer;
};

} // namespace callie
