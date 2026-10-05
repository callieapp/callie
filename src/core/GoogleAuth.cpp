#include "callie/GoogleAuth.h"

#include "callie/Logging.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QOAuth2AuthorizationCodeFlow>
#include <QOAuthHttpServerReplyHandler>
#include <QTimer>
#include <QUrlQuery>

// Qt 6.9 replaced several QtNetworkAuth calls. Debian trixie ships 6.8, which
// lacks the replacements, while newer Qt warns on the originals.
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
#define CALLIE_QT_OAUTH_69
#endif

namespace callie {

GoogleAuth::GoogleAuth(const GoogleClientConfig &client, QObject *parent)
    : QObject(parent), m_client(client), m_flow(new QOAuth2AuthorizationCodeFlow(this)),
      m_network(new QNetworkAccessManager(this)), m_signInTimer(new QTimer(this))
{
    m_signInTimer->setSingleShot(true);
    m_signInTimer->setInterval(std::chrono::minutes(5));
    connect(m_signInTimer, &QTimer::timeout, this,
            [this] { fail(tr("timed out waiting for Google sign-in")); });

    setEndpoints(QUrl(QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth")),
                 QUrl(QStringLiteral("https://oauth2.googleapis.com/token")));
    m_flow->setClientIdentifier(client.clientId);
    m_flow->setClientIdentifierSharedKey(client.clientSecret);
#ifdef CALLIE_QT_OAUTH_69
    QSet<QByteArray> scopeTokens;
    for (const QString &scope : scopes().split(u' '))
        scopeTokens.insert(scope.toLatin1());
    m_flow->setRequestedScopeTokens(scopeTokens);
#else
    m_flow->setScope(scopes());
#endif
    m_flow->setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);

    // Google only issues a refresh token for offline access, and only on a
    // consent screen, so re-adding an account must not silently skip it.
    m_flow->setModifyParametersFunction(
        [](QAbstractOAuth::Stage stage, QMultiMap<QString, QVariant> *parameters) {
            if (stage != QAbstractOAuth::Stage::RequestingAuthorization)
                return;
            parameters->insert(QStringLiteral("access_type"), QStringLiteral("offline"));
            parameters->insert(QStringLiteral("prompt"), QStringLiteral("consent"));
        });

    connect(m_flow, &QAbstractOAuth::authorizeWithBrowser, this, &GoogleAuth::authorizeUrlReady);
    connect(m_flow, &QAbstractOAuth::granted, this, &GoogleAuth::onGranted);
    const auto onServerError = [this](const QString &error, const QString &description,
                                      const QUrl &) {
        fail(description.isEmpty() ? error : description);
    };
#ifdef CALLIE_QT_OAUTH_69
    connect(m_flow, &QAbstractOAuth2::serverReportedErrorOccurred, this, onServerError);
#else
    connect(m_flow, &QAbstractOAuth2::error, this, onServerError);
#endif
    connect(m_flow, &QAbstractOAuth::requestFailed, this,
            [this](QAbstractOAuth::Error) { fail(tr("the request to Google failed")); });
}

GoogleAuth::~GoogleAuth() = default;

QString GoogleAuth::scopes()
{
    return QStringLiteral("https://www.googleapis.com/auth/calendar.events "
                          "https://www.googleapis.com/auth/calendar.calendarlist.readonly");
}

void GoogleAuth::setEndpoints(const QUrl &authorization, const QUrl &token)
{
    m_flow->setAuthorizationUrl(authorization);
    setTokenUrl(token);
}

void GoogleAuth::setTokenUrl(const QUrl &token)
{
    m_tokenUrl = token;
#ifdef CALLIE_QT_OAUTH_69
    m_flow->setTokenUrl(token);
#else
    m_flow->setAccessTokenUrl(token);
#endif
}

void GoogleAuth::authorize()
{
    if (!m_handler) {
        // The default address is QHostAddress::Any, which would expose the
        // callback port, and with it the authorization code, to the network.
        m_handler = new QOAuthHttpServerReplyHandler(QHostAddress::LocalHost, 0, this);
#ifdef CALLIE_QT_OAUTH_69
        // Google recommends the IP literal over "localhost" for loopback redirects.
        m_handler->setCallbackHost(QStringLiteral("127.0.0.1"));
#endif
        m_handler->setCallbackText(tr("Callie is connected. You can close this tab."));
        m_flow->setReplyHandler(m_handler);
    }
    // A finished attempt closes the listener; a retry or a second account needs
    // it reopened, on loopback again rather than the QHostAddress::Any default.
    if (!m_handler->isListening() && !m_handler->listen(QHostAddress::LocalHost, 0)) {
        fail(tr("could not listen for the Google sign-in callback"));
        return;
    }
    qCInfo(lcAuth) << "waiting for Google sign-in at" << callbackUrl().toString();
    m_signInTimer->start();
    m_flow->grant();
}

void GoogleAuth::setSignInTimeout(std::chrono::milliseconds timeout)
{
    m_signInTimer->setInterval(timeout);
}

void GoogleAuth::refresh(const QString &refreshToken)
{
    // Done by hand rather than through the flow, which drops the body of an
    // error response and with it Google's reason for refusing.
    qCDebug(lcAuth) << "refreshing the access token";
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    form.addQueryItem(QStringLiteral("refresh_token"), refreshToken);
    form.addQueryItem(QStringLiteral("client_id"), m_client.clientId);
    form.addQueryItem(QStringLiteral("client_secret"), m_client.clientSecret);
    QNetworkRequest request(m_tokenUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));

    QNetworkReply *reply = m_network->post(request, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply, refreshToken] {
        reply->deleteLater();
        const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
        const QString accessToken = body[u"access_token"].toString();
        if (reply->error() == QNetworkReply::NoError && !accessToken.isEmpty()) {
            const int expiresIn = body[u"expires_in"].toInt();
            Q_EMIT granted(GoogleTokens{
                accessToken,
                body[u"refresh_token"].toString(refreshToken),
                expiresIn > 0 ? QDateTime::currentDateTimeUtc().addSecs(expiresIn) : QDateTime(),
            });
            return;
        }

        const QString error = body[u"error"].toString();
        const QString description = body[u"error_description"].toString();
        if (error == QLatin1String("invalid_grant"))
            fail(tr("Google no longer accepts the saved sign-in (%1). Connect the account "
                    "again.")
                     .arg(description.isEmpty() ? error : description));
        else if (!error.isEmpty())
            fail(tr("Google refused to refresh the sign-in: %1")
                     .arg(description.isEmpty() ? error : description));
        else if (reply->error() == QNetworkReply::NoError)
            fail(tr("Google returned no access token"));
        else
            fail(tr("could not refresh the Google sign-in: %1").arg(reply->errorString()));
    });
}

QUrl GoogleAuth::callbackUrl() const
{
    return m_handler ? QUrl(m_handler->callback()) : QUrl();
}

void GoogleAuth::onGranted()
{
    m_signInTimer->stop();
    qCInfo(lcAuth) << "Google granted access";
    if (m_handler)
        m_handler->close();

    const GoogleTokens tokens{m_flow->token(), m_flow->refreshToken(), m_flow->expirationAt()};
    if (tokens.refreshToken.isEmpty()) {
        fail(tr("Google did not return a refresh token"));
        return;
    }
    Q_EMIT granted(tokens);
}

void GoogleAuth::fail(const QString &message)
{
    m_signInTimer->stop();
    // Info, not warning: whoever started the flow reports the failure to the user.
    qCInfo(lcAuth) << "sign-in failed:" << message;
    if (m_handler)
        m_handler->close();
    Q_EMIT failed(message);
}

} // namespace callie
