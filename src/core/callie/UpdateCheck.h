#pragma once

#include "Releases.h"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QObject>

#include <functional>
#include <optional>

namespace callie {

/// Remembers Callie's latest release, asking GitHub for it at most once a
/// day, so the app and the CLI can say when a newer one is out.
class UpdateCheck : public QObject
{
    Q_OBJECT

public:
    explicit UpdateCheck(QString path = defaultPath(), QObject *parent = nullptr);

    /// Where the latest release is remembered, beside the cache.
    [[nodiscard]] static QString defaultPath();
    /// Whether to look at all: not for development builds, not when the user
    /// said no, and not when CALLIE_NO_UPDATE_CHECK is set.
    [[nodiscard]] bool wanted(bool setting) const;
    /// A development build updates from its source, so never looks.
    void setDevel(bool devel) { m_devel = devel; }

    void setApi(const QUrl &api) { m_api = api; }
    void setNow(std::function<QDateTime()> now) { m_now = std::move(now); }

    /// The latest release remembered, if it is newer than `running`.
    [[nodiscard]] std::optional<Release> newerThan(const QVersionNumber &running) const;
    /// A day has passed since GitHub was last asked, or it never was.
    [[nodiscard]] bool due() const;
    /// Notes that GitHub is being asked now, so others wait a day too.
    void markAsked();
    /// Asks GitHub for the latest release and remembers it; then checked().
    void check();

Q_SIGNALS:
    void checked();

private:
    void remember(const QJsonObject &entry);
    [[nodiscard]] QJsonObject read() const;

    QString m_path;
    QUrl m_api;
    std::function<QDateTime()> m_now;
    bool m_devel;
    QNetworkAccessManager m_network;
};

} // namespace callie
