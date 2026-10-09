#pragma once

#include "Account.h"
#include "GoogleClientConfig.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QUrl>

#include <functional>

namespace callie {

class TokenStore;

/// Access tokens for connected Google accounts, refreshed from the refresh
/// token in the keyring and cached until shortly before they expire.
class GoogleTokenProvider : public QObject
{
    Q_OBJECT

public:
    using TokenResult = std::function<void(const QString &accessToken, const QString &error)>;

    GoogleTokenProvider(const GoogleClientConfig &client, TokenStore &tokens,
                        QObject *parent = nullptr);

    /// Points refreshes at a fake server in tests.
    void setTokenUrl(const QUrl &url) { m_tokenUrl = url; }

    /// Calls `result` exactly once, never synchronously. Concurrent requests
    /// for one account share a single refresh.
    void accessToken(const Account &account, TokenResult result);

    /// Drops a cached token that Google rejected, so the next request refreshes.
    void invalidate(const Account &account);
    /// Drops everything known about a removed account, granted scopes included.
    void forget(const Account &account);

    /// What Callie asks for that the account has not granted, as of its last
    /// refresh; empty until then, or when Google does not say.
    [[nodiscard]] QStringList missingScopes(const Account &account) const;

Q_SIGNALS:
    /// A refresh said what the account has granted.
    void scopesKnown(const callie::Account &account);

private:
    struct Cached
    {
        QString accessToken;
        QDateTime expiresAt;
    };

    void refresh(const Account &account, const QString &refreshToken);
    void finish(const QString &key, const QString &accessToken, const QString &error);

    GoogleClientConfig m_client;
    TokenStore &m_tokens;
    QUrl m_tokenUrl;
    QHash<QString, Cached> m_cache;
    QHash<QString, QStringList> m_granted;
    QHash<QString, QList<TokenResult>> m_waiting;
};

} // namespace callie
