#pragma once

#include "FakeHttpServer.h"

#include "callie/GoogleAuth.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QUrlQuery>

/// Plays the parts of Google's sign-in page and the user's browser, so tests can
/// run the real GoogleAuth against a FakeHttpServer token endpoint.
namespace GoogleAuthDriver {

/// Points `auth` at `tokenServer` before a flow starts.
inline void useTokenServer(callie::GoogleAuth &auth, const FakeHttpServer &tokenServer)
{
    auth.setEndpoints(QUrl(QStringLiteral("https://accounts.example/auth")),
                      tokenServer.url(QStringLiteral("/token")));
}

/// Starts authorization and returns the URL the browser would open.
inline QUrl startAuthorization(callie::GoogleAuth &auth, const FakeHttpServer &tokenServer)
{
    useTokenServer(auth, tokenServer);
    QSignalSpy ready(&auth, &callie::GoogleAuth::authorizeUrlReady);
    auth.authorize();
    if (ready.isEmpty() && !ready.wait(2000))
        return {};
    return ready.first().first().toUrl();
}

/// Google redirecting to the loopback callback after the user consents.
inline void redirectBack(QNetworkAccessManager &network, const callie::GoogleAuth &auth,
                         const QString &state)
{
    QUrl callback = auth.callbackUrl();
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("code"), QStringLiteral("the-code"));
    query.addQueryItem(QStringLiteral("state"), state);
    callback.setQuery(query);
    QNetworkReply *reply = network.get(QNetworkRequest(callback));
    QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

/// Answers the redirect for whatever authorization `auth` starts next.
inline void approveNextAuthorization(QNetworkAccessManager &network, callie::GoogleAuth &auth)
{
    QObject::connect(
        &auth, &callie::GoogleAuth::authorizeUrlReady, &auth,
        [&network, &auth](const QUrl &url) {
            const QString state =
                QUrlQuery(url).queryItemValue(QStringLiteral("state"), QUrl::FullyDecoded);
            redirectBack(network, auth, state);
        },
        Qt::SingleShotConnection);
}

} // namespace GoogleAuthDriver
