#pragma once

#include "FakeHttpServer.h"

#include "callie/GoogleAuth.h"
#include "callie/MicrosoftAuth.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QUrlQuery>

/// Plays the parts of the sign-in page and the user's browser, so tests can run
/// the real GoogleAuth or MicrosoftAuth against a FakeHttpServer token endpoint.
namespace AuthDriver {

/// Points `auth` at `tokenServer` before a flow starts.
template<typename Auth>
inline void useTokenServer(Auth &auth, const FakeHttpServer &tokenServer)
{
    auth.setEndpoints(QUrl(QStringLiteral("https://accounts.example/auth")),
                      tokenServer.url(QStringLiteral("/token")));
}

/// Starts authorization and returns the URL the browser would open.
template<typename Auth>
inline QUrl startAuthorization(Auth &auth, const FakeHttpServer &tokenServer)
{
    useTokenServer(auth, tokenServer);
    QSignalSpy ready(&auth, &Auth::authorizeUrlReady);
    auth.authorize();
    if (ready.isEmpty() && !ready.wait(2000))
        return {};
    return ready.first().first().toUrl();
}

/// The provider redirecting to the loopback callback after the user consents.
template<typename Auth>
inline void redirectBack(QNetworkAccessManager &network, const Auth &auth, const QString &state)
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
template<typename Auth>
inline void approveNextAuthorization(QNetworkAccessManager &network, Auth &auth)
{
    QObject::connect(
        &auth, &Auth::authorizeUrlReady, &auth,
        [&network, &auth](const QUrl &url) {
            const QString state =
                QUrlQuery(url).queryItemValue(QStringLiteral("state"), QUrl::FullyDecoded);
            redirectBack(network, auth, state);
        },
        Qt::SingleShotConnection);
}

} // namespace AuthDriver
