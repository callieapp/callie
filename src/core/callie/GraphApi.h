#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace callie {

/// A failed Graph call. `status` is the HTTP status, or 0 when no response came.
struct GraphError
{
    int status = 0;
    QString message;

    explicit operator bool() const { return !message.isEmpty(); }
    /// The access token expired or was revoked; refresh it and try once more.
    [[nodiscard]] bool unauthorized() const { return status == 401; }
    /// Worth trying again later: offline, throttled or failing.
    [[nodiscard]] bool later() const
    {
        return status == 0 || status == 408 || status == 429 || status >= 500;
    }
};

struct GraphUser
{
    QString id;
    /// The address the account signs in with and receives invitations at.
    QString address;
    QString name;
};

/// Microsoft Graph v1.0, as Callie uses it. Every call takes an access token
/// and follows paging itself, so a list is always complete.
class GraphApi : public QObject
{
    Q_OBJECT

public:
    using Answer = std::function<void(const QJsonObject &body, const GraphError &error)>;
    using Pages = std::function<void(const QList<QJsonObject> &items, const GraphError &error)>;

    explicit GraphApi(QNetworkAccessManager *network, QObject *parent = nullptr);

    /// Points calls at a fake server in tests.
    void setBaseUrl(const QUrl &url) { m_baseUrl = url; }

    void fetchMe(const QString &accessToken,
                 std::function<void(const GraphUser &user, const GraphError &error)> result);

    /// One request to `path` under the base address, with `body` when given.
    void send(const QString &accessToken, const QByteArray &method, const QString &path,
              const QJsonObject &body, Answer answer);
    /// Every page of a list at `path`, items in order.
    void list(const QString &accessToken, const QString &path, Pages pages);

private:
    void request(const QString &accessToken, const QByteArray &method, const QUrl &url,
                 const QJsonObject &body, Answer answer);
    void nextPage(const QString &accessToken, const QUrl &url, QList<QJsonObject> items,
                  Pages pages);

    QPointer<QNetworkAccessManager> m_network;
    QUrl m_baseUrl = QUrl(QStringLiteral("https://graph.microsoft.com/v1.0/"));
};

} // namespace callie
