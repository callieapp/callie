#include "callie/GoogleTokenProvider.h"

#include "callie/GoogleAuth.h"
#include "callie/Logging.h"
#include "callie/TokenStore.h"

#include <QPointer>

namespace callie {

namespace {

// Margin for clock skew and for requests that start just before expiry.
constexpr int kExpiryMarginSeconds = 60;

QString keyFor(const Account &account)
{
    return account.provider + u'/' + account.id;
}

} // namespace

GoogleTokenProvider::GoogleTokenProvider(const GoogleClientConfig &client, TokenStore &tokens,
                                         QObject *parent)
    : QObject(parent), m_client(client), m_tokens(tokens)
{}

void GoogleTokenProvider::accessToken(const Account &account, TokenResult result)
{
    const QString key = keyFor(account);
    const auto cached = m_cache.constFind(key);
    if (cached != m_cache.cend() &&
        QDateTime::currentDateTimeUtc().addSecs(kExpiryMarginSeconds) < cached->expiresAt) {
        QMetaObject::invokeMethod(
            this, [result = std::move(result), token = cached->accessToken] { result(token, {}); },
            Qt::QueuedConnection);
        return;
    }

    QList<TokenResult> &waiting = m_waiting[key];
    waiting.append(std::move(result));
    if (waiting.size() > 1)
        return;

    m_tokens.read(account, [self = QPointer(this), account, key](const QString &refreshToken,
                                                                 const QString &error) {
        if (!self)
            return;
        if (!error.isEmpty())
            self->finish(key, {}, tr("could not read the stored token: %1").arg(error));
        else if (refreshToken.isEmpty())
            self->finish(key, {},
                         tr("no stored token for %1, connect the account again").arg(account.id));
        else
            self->refresh(account, refreshToken);
    });
}

void GoogleTokenProvider::invalidate(const Account &account)
{
    m_cache.remove(keyFor(account));
}

void GoogleTokenProvider::refresh(const Account &account, const QString &refreshToken)
{
    const QString key = keyFor(account);
    auto *auth = new GoogleAuth(m_client, this);
    if (m_tokenUrl.isValid())
        auth->setTokenUrl(m_tokenUrl);

    connect(auth, &GoogleAuth::granted, this,
            [this, auth, account, key, refreshToken](const GoogleTokens &tokens) {
                auth->deleteLater();
                m_cache.insert(key, {tokens.accessToken, tokens.expiresAt});
                // Google keeps the refresh token stable today, but if it ever
                // rotates one the old one stops working.
                if (tokens.refreshToken != refreshToken) {
                    m_tokens.write(account, tokens.refreshToken, [](const QString &error) {
                        if (!error.isEmpty())
                            qCWarning(lcAuth) << "could not store the new refresh token:" << error;
                    });
                }
                finish(key, tokens.accessToken, {});
            });
    connect(auth, &GoogleAuth::failed, this, [this, auth, key](const QString &message) {
        auth->deleteLater();
        finish(key, {}, message);
    });
    auth->refresh(refreshToken);
}

void GoogleTokenProvider::finish(const QString &key, const QString &accessToken,
                                 const QString &error)
{
    const QList<TokenResult> waiting = m_waiting.take(key);
    for (const TokenResult &result : waiting)
        result(accessToken, error);
}

} // namespace callie
