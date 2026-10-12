#pragma once

#include "VdirSource.h"

#include <QHash>
#include <QPointer>
#include <QUrl>

#include <chrono>

class QNetworkAccessManager;

namespace callie {

/// Calendars subscribed to by link, such as holidays or a team's schedule:
/// read-only copies of each feed, kept as vdir folders beside the link and
/// fetched again every hour. A feed that has not changed is not downloaded.
class Subscriptions : public VdirSource
{
    Q_OBJECT

public:
    /// `network` fetches the feeds; without one they are only read from disk.
    Subscriptions(QString root, QNetworkAccessManager *network, QObject *parent = nullptr);

    /// `$XDG_DATA_HOME/callie/subscriptions`
    [[nodiscard]] static QString defaultRoot();
    /// The CalendarInfo::account its calendars are grouped under.
    static constexpr QLatin1StringView kAccount{"subscriptions"};
    /// How long a feed is kept before it is fetched again.
    static constexpr std::chrono::minutes kEvery{60};

    /// The address a link is fetched from: webcal:// and webcals:// as
    /// https://, and http(s) as it is. Invalid for anything else.
    [[nodiscard]] static QUrl feedUrl(const QString &link);

    /// Subscribes to `link` and fetches it; `name`, when given, names it, else
    /// the feed's own name does. Returns its id, or empty with `error` set.
    QString add(const QString &link, const QString &name, QString *error);
    /// Stops the subscription and forgets its copy.
    bool remove(const QString &id, QString *error);
    /// The link a subscription was made from.
    [[nodiscard]] QString link(const QString &id) const;

    /// Fetches the feeds not fetched in the last kEvery.
    void refresh() override;
    /// Fetches every feed now.
    void fetchAll();

    [[nodiscard]] bool syncing() const override { return !m_fetching.isEmpty(); }
    [[nodiscard]] QDateTime lastSynced() const override;
    [[nodiscard]] QString lastError() const override;
    [[nodiscard]] QVariantList syncReport() const override;

Q_SIGNALS:
    /// Every fetch under way has finished.
    void fetched();

private:
    /// What is kept about a feed between fetches, in a hidden file beside it.
    struct State
    {
        QString etag;
        QString modified;
        QDateTime fetched;
        QString error;
    };

    void scan();
    void fetch(const Folder &folder);
    void finish(const Folder &folder, const State &state);
    [[nodiscard]] static State readState(const QString &path);
    static void writeState(const QString &path, const State &state);

    QString m_root;
    QPointer<QNetworkAccessManager> m_network;
    /// The ids of the feeds being fetched.
    QSet<QString> m_fetching;
};

} // namespace callie
