#pragma once

#include <QObject>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

namespace callie {

/// Google Calendar API v3. Every call takes an access token from GoogleAuth.
class GoogleCalendarApi : public QObject
{
    Q_OBJECT

public:
    using IdResult = std::function<void(const QString &id, const QString &error)>;

    explicit GoogleCalendarApi(QNetworkAccessManager *network, QObject *parent = nullptr);

    /// Points requests at a fake server in tests. Must end with a slash.
    void setBaseUrl(const QUrl &url) { m_baseUrl = url; }

    /// The primary calendar's id, which Google sets to the account's email.
    void fetchPrimaryCalendarId(const QString &accessToken, IdResult result);

private:
    QNetworkAccessManager *m_network;
    QUrl m_baseUrl{QStringLiteral("https://www.googleapis.com/calendar/v3/")};
};

} // namespace callie
