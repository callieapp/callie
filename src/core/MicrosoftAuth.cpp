#include "callie/MicrosoftAuth.h"

#include "callie/Logging.h"
#include "callie/SignInPage.h"
#include "callie/ThemeLoader.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QOAuth2AuthorizationCodeFlow>
#include <QOAuthHttpServerReplyHandler>
#include <QTimer>
#include <QUrlQuery>

// As in GoogleAuth: Qt 6.9 renamed calls that Debian trixie's 6.8 lacks.
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
#define CALLIE_QT_OAUTH_69
#endif

namespace callie {

MicrosoftAuth::MicrosoftAuth(const MicrosoftClientConfig &client, QObject *parent)
    : QObject(parent), m_client(client), m_flow(new QOAuth2AuthorizationCodeFlow(this)),
      m_network(new QNetworkAccessManager(this)), m_signInTimer(new QTimer(this))
{
    m_signInTimer->setSingleShot(true);
    m_signInTimer->setInterval(std::chrono::minutes(5));
    connect(m_signInTimer, &QTimer::timeout, this,
            [this] { fail(tr("timed out waiting for Microsoft sign-in")); });

    // "common" takes personal accounts as well as work and school ones.
    setEndpoints(
        QUrl(QStringLiteral("https://login.microsoftonline.com/common/oauth2/v2.0/authorize")),
        QUrl(QStringLiteral("https://login.microsoftonline.com/common/oauth2/v2.0/token")));
    m_flow->setClientIdentifier(client.clientId);
#ifdef CALLIE_QT_OAUTH_69
    QSet<QByteArray> scopeTokens;
    for (const QString &scope : scopes().split(u' '))
        scopeTokens.insert(scope.toLatin1());
    m_flow->setRequestedScopeTokens(scopeTokens);
#else
    m_flow->setScope(scopes());
#endif
    m_flow->setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);
    m_flow->setModifyParametersFunction(
        [this](QAbstractOAuth::Stage stage, QMultiMap<QString, QVariant> *parameters) {
            if (stage != QAbstractOAuth::Stage::RequestingAuthorization)
                return;
            // Lets someone signed in to several accounts pick the one to add.
            parameters->insert(QStringLiteral("prompt"), QStringLiteral("select_account"));
            if (!m_loginHint.isEmpty())
                parameters->insert(QStringLiteral("login_hint"), m_loginHint);
        });

    connect(m_flow, &QAbstractOAuth::authorizeWithBrowser, this, &MicrosoftAuth::authorizeUrlReady);
    connect(m_flow, &QAbstractOAuth::granted, this, &MicrosoftAuth::onGranted);
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
            [this](QAbstractOAuth::Error) { fail(tr("the request to Microsoft failed")); });
}

MicrosoftAuth::~MicrosoftAuth() = default;

QString MicrosoftAuth::scopes()
{
    // offline_access is what brings a refresh token.
    return QStringLiteral("offline_access User.Read Calendars.ReadWrite People.Read");
}

void MicrosoftAuth::setEndpoints(const QUrl &authorization, const QUrl &token)
{
    m_flow->setAuthorizationUrl(authorization);
    setTokenUrl(token);
}

void MicrosoftAuth::setTokenUrl(const QUrl &token)
{
    m_tokenUrl = token;
#ifdef CALLIE_QT_OAUTH_69
    m_flow->setTokenUrl(token);
#else
    m_flow->setAccessTokenUrl(token);
#endif
}

void MicrosoftAuth::authorize()
{
    if (!m_handler) {
        // Loopback only, so the code never reaches the network.
        m_handler = new QOAuthHttpServerReplyHandler(QHostAddress::LocalHost, 0, this);
#ifdef CALLIE_QT_OAUTH_69
        // Microsoft, like Google, recommends the IP literal over "localhost".
        m_handler->setCallbackHost(QStringLiteral("127.0.0.1"));
#endif
        m_handler->setCallbackText(signInPage(ThemeLoader::defaultTheme()));
        m_flow->setReplyHandler(m_handler);
    }
    if (!m_handler->isListening() && !m_handler->listen(QHostAddress::LocalHost, 0)) {
        fail(tr("could not listen for the Microsoft sign-in callback"));
        return;
    }
    qCInfo(lcAuth) << "waiting for Microsoft sign-in at" << m_handler->callback();
    m_signInTimer->start();
    m_flow->grant();
}

void MicrosoftAuth::setSignInTimeout(std::chrono::milliseconds timeout)
{
    m_signInTimer->setInterval(timeout);
}

void MicrosoftAuth::refresh(const QString &refreshToken)
{
    // By hand, as in GoogleAuth, to keep Microsoft's reason when it refuses.
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    form.addQueryItem(QStringLiteral("refresh_token"), refreshToken);
    form.addQueryItem(QStringLiteral("client_id"), m_client.clientId);
    form.addQueryItem(QStringLiteral("scope"), scopes());
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
            Q_EMIT granted(MicrosoftTokens{
                accessToken, body[u"refresh_token"].toString(refreshToken),
                expiresIn > 0 ? QDateTime::currentDateTimeUtc().addSecs(expiresIn) : QDateTime()});
            return;
        }
        const QString error = body[u"error"].toString();
        // Microsoft's descriptions start with a code, as "AADSTS70008: ...".
        const QString description = body[u"error_description"].toString().section(u'\n', 0, 0);
        if (error == QLatin1String("invalid_grant"))
            fail(tr("Microsoft no longer accepts the saved sign-in (%1). Connect the account "
                    "again.")
                     .arg(description.isEmpty() ? error : description));
        else if (!error.isEmpty())
            fail(tr("Microsoft refused to refresh the sign-in: %1")
                     .arg(description.isEmpty() ? error : description));
        else
            fail(tr("could not refresh the Microsoft sign-in: %1").arg(reply->errorString()));
    });
}

QUrl MicrosoftAuth::callbackUrl() const
{
    return m_handler ? QUrl(m_handler->callback()) : QUrl();
}

void MicrosoftAuth::onGranted()
{
    m_signInTimer->stop();
    qCInfo(lcAuth) << "Microsoft granted access";
    if (m_handler)
        m_handler->close();
    const MicrosoftTokens tokens{m_flow->token(), m_flow->refreshToken(), m_flow->expirationAt()};
    if (tokens.refreshToken.isEmpty()) {
        fail(tr("Microsoft did not return a refresh token"));
        return;
    }
    Q_EMIT granted(tokens);
}

void MicrosoftAuth::fail(const QString &message)
{
    m_signInTimer->stop();
    qCInfo(lcAuth) << "Microsoft sign-in failed:" << message;
    if (m_handler)
        m_handler->close();
    Q_EMIT failed(message);
}

} // namespace callie
