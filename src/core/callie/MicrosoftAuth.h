#pragma once

#include "MicrosoftClientConfig.h"

#include <QDateTime>
#include <QObject>
#include <QUrl>

#include <chrono>

class QNetworkAccessManager;
class QOAuth2AuthorizationCodeFlow;
class QTimer;

namespace callie {

class OAuthReplyHandler;

struct MicrosoftTokens
{
    QString accessToken;
    /// Microsoft gives a new one with every refresh; the old one soon stops working.
    QString refreshToken;
    QDateTime expiresAt;
};

/// OAuth 2.0 with the Microsoft identity platform as a public client: the
/// authorization code flow with PKCE and a loopback redirect, for personal,
/// work and school accounts alike. Opening the browser is left to the caller.
class MicrosoftAuth : public QObject
{
    Q_OBJECT

public:
    explicit MicrosoftAuth(const MicrosoftClientConfig &client, QObject *parent = nullptr);
    ~MicrosoftAuth() override;

    /// Space-separated scopes requested.
    [[nodiscard]] static QString scopes();

    /// Points the flow at a fake server in tests.
    void setEndpoints(const QUrl &authorization, const QUrl &token);
    void setTokenUrl(const QUrl &token);
    /// Picks this account on Microsoft's sign-in page, for signing in again.
    void setLoginHint(const QString &email) { m_loginHint = email; }

    /// Starts the loopback listener and emits authorizeUrlReady.
    void authorize();
    /// Exchanges a refresh token for a new access token and refresh token.
    void refresh(const QString &refreshToken);
    void setSignInTimeout(std::chrono::milliseconds timeout);
    /// The redirect URI, valid once authorize() has been called.
    [[nodiscard]] QUrl callbackUrl() const;

Q_SIGNALS:
    void authorizeUrlReady(const QUrl &url);
    void granted(const callie::MicrosoftTokens &tokens);
    void failed(const QString &message);

private:
    void onGranted();
    void fail(const QString &message);

    MicrosoftClientConfig m_client;
    QString m_loginHint;
    QOAuth2AuthorizationCodeFlow *m_flow;
    QNetworkAccessManager *m_network;
    QUrl m_tokenUrl;
    OAuthReplyHandler *m_handler = nullptr;
    QTimer *m_signInTimer;
};

} // namespace callie
