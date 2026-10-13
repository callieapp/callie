#pragma once

#include <QColor>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace callie {

/// One calendar on a CalDAV server.
struct DavCalendar
{
    QUrl url;
    QString displayName;
    QColor color;
    bool writable = false;
    /// The server's version of the whole calendar, which changes with any event.
    QString ctag;
    /// Where incremental sync starts from, when the server offers it.
    QString syncToken;
};

/// What discovery finds for an account.
struct DavAccount
{
    QUrl principal;
    QUrl home;
    /// The user's own addresses as the server knows them, which pick out their
    /// answers to invitations.
    QStringList addresses;
    QList<DavCalendar> calendars;
};

/// One event resource: where it is, its version, and its iCalendar text when fetched.
struct DavItem
{
    QUrl href;
    QString etag;
    QByteArray data = {};
};

/// A failed request. `status` is the HTTP status, or 0 when no response arrived.
struct DavError
{
    int status = 0;
    QString message;

    explicit operator bool() const { return !message.isEmpty(); }
    /// The name or password was turned down.
    [[nodiscard]] bool isAuth() const { return status == 401; }
    /// The event changed on the server since it was read.
    [[nodiscard]] bool isConflict() const { return status == 412; }
};

/// The CalDAV requests Callie makes (RFC 4791 and 6578), over Basic
/// authentication. Each call answers its callback exactly once.
class CalDavClient : public QObject
{
    Q_OBJECT

public:
    explicit CalDavClient(QNetworkAccessManager *network, QObject *parent = nullptr);

    void setCredentials(const QString &user, const QString &password);

    /// Whether a redirect from `from` to `to` is followed: never from https
    /// down to http.
    [[nodiscard]] static bool mayFollow(const QUrl &from, const QUrl &to);

    /// Finds the account's calendars from the server's address, through the
    /// well-known address when the one given does not lead to them.
    void discover(const QUrl &server,
                  std::function<void(const DavAccount &, const DavError &)> done);
    void calendars(const QUrl &home,
                   std::function<void(const QList<DavCalendar> &, const DavError &)> done);

    /// What changed since `token`: changed items without their data, removed
    /// items, and the token to start from next. An empty token lists everything.
    /// A token the server no longer knows fails with status 403 or 409.
    struct Changes
    {
        QList<DavItem> changed;
        QList<QUrl> removed;
        QString token;
    };
    void changes(const QUrl &calendar, const QString &token,
                 std::function<void(const Changes &, const DavError &)> done);
    /// Every item's address and version, for servers without sync tokens.
    void list(const QUrl &calendar,
              std::function<void(const QList<DavItem> &, const DavError &)> done);
    /// The items at `hrefs`, with their data, in batches.
    void fetch(const QUrl &calendar, const QList<QUrl> &hrefs,
               std::function<void(const QList<DavItem> &, const DavError &)> done);
    /// Writes `data` at `href`, only over version `etag`, or only where nothing
    /// is when `etag` is empty. Gives the new version when the server says it.
    void put(const QUrl &href, const QByteArray &data, const QString &etag,
             std::function<void(const QString &etag, const DavError &)> done);
    /// Renames and recolors a calendar on the server; an empty name or an
    /// invalid color leaves that one as it is.
    void setLook(const QUrl &calendar, const QString &name, const QColor &color,
                 std::function<void(const DavError &)> done);
    /// Deletes the item, only at version `etag` when given. Gone already counts.
    void remove(const QUrl &href, const QString &etag, std::function<void(const DavError &)> done);

private:
    struct Response;
    using Answered = std::function<void(QNetworkReply *reply, const DavError &error)>;

    /// Sends a request, following redirects with the same method. The
    /// password goes only over https, or to this machine, and only to hosts
    /// the account trusts.
    void send(const QByteArray &method, const QUrl &url, const QByteArray &body,
              const QList<std::pair<QByteArray, QByteArray>> &headers, Answered answered,
              int redirects = 0);
    void findPrincipal(const QUrl &url, bool wellKnownTried,
                       std::function<void(const DavAccount &, const DavError &)> done);
    [[nodiscard]] static QList<Response> multistatus(const QByteArray &xml, const QUrl &base,
                                                     QString *syncToken = nullptr);
    [[nodiscard]] DavError failure(QNetworkReply *reply) const;
    /// Trusts the host of `named`, when the answer naming it came from a trusted host.
    void learnHost(QNetworkReply *reply, const QUrl &named);

    QPointer<QNetworkAccessManager> m_network;
    QString m_user;
    QString m_password;
    /// The hosts the password goes to: the server's own, and those it names in
    /// its signed-in answers. Redirects elsewhere go without it.
    QSet<QString> m_trusted;
};

} // namespace callie
