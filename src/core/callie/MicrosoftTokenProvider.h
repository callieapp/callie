#pragma once

#include "Account.h"
#include "MicrosoftClientConfig.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QUrl>

#include <functional>

namespace callie {

class TokenStore;

/// Access tokens for connected Microsoft accounts, refreshed from the refresh
/// token in the keyring and cached until shortly before they expire. Each
/// refresh brings a new refresh token, which replaces the stored one.
class MicrosoftTokenProvider : public QObject
{
    Q_OBJECT

public:
    using TokenResult = std::function<void(const QString &accessToken, const QString &error)>;

    MicrosoftTokenProvider(const MicrosoftClientConfig &client, TokenStore &tokens,
                           QObject *parent = nullptr);

    /// Points refreshes at a fake server in tests.
    void setTokenUrl(const QUrl &url) { m_tokenUrl = url; }

    /// Calls `result` exactly once, never synchronously. Requests for one
    /// account share a single refresh, so one refresh token is spent at a time.
    void accessToken(const Account &account, TokenResult result);
    /// Drops a cached token that Microsoft turned down.
    void invalidate(const Account &account);

private:
    struct Cached
    {
        QString accessToken;
        QDateTime expiresAt;
    };

    void refresh(const Account &account, const QString &refreshToken);
    void finish(const QString &key, const QString &accessToken, const QString &error);

    MicrosoftClientConfig m_client;
    TokenStore &m_tokens;
    QUrl m_tokenUrl;
    QHash<QString, Cached> m_cache;
    QHash<QString, QList<TokenResult>> m_waiting;
};

} // namespace callie
