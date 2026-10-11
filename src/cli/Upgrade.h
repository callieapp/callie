#pragma once

#include "callie/Releases.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>

#include <functional>
#include <memory>
#include <optional>

namespace callie::cli {

/// `callie upgrade` and `callie downgrade`: finds the release asked for,
/// downloads this system's package, checks it against the checksum GitHub
/// records, and installs it with dnf or apt, as the installer script does.
class Upgrade : public QObject
{
    Q_OBJECT

public:
    struct Options
    {
        /// Going back to an older release rather than forward.
        bool downgrade = false;
        /// A release by version, such as "0.2.1"; otherwise the latest, or
        /// for a downgrade the newest one before the installed one.
        std::optional<QString> version;
        /// Install even when that release is the one installed.
        bool force = false;
        /// Say what would happen, and change nothing.
        bool dryRun = false;
    };
    using Done = std::function<void(int exitCode)>;
    /// Runs a command with the terminal, so sudo can ask for a password, and
    /// gives its exit code.
    using Runner = std::function<int(const QStringList &command)>;

    Upgrade(QTextStream &out, QTextStream &err, QObject *parent = nullptr);

    void setApi(const QUrl &api) { m_api = api; }
    void setSystem(const PackageSystem &system) { m_system = system; }
    /// The installed package's version, empty when Callie is not installed
    /// from a package. Asks rpm or dpkg unless set.
    void setInstalled(const QString &version) { m_installed = version; }
    void setRunner(Runner runner) { m_runner = std::move(runner); }
    /// A development build updates from its source instead.
    void setDevel(bool devel) { m_devel = devel; }

    void run(const Options &options, Done done);

private:
    void chooseFrom(const QList<Release> &releases);
    void install(const Release &release);
    void finish(int code);
    [[nodiscard]] QString installedVersion();
    /// GETs `url` and hands the body on, or says why it could not.
    void fetch(const QUrl &url, const QString &what, std::function<void(QByteArray)> then);

    QTextStream &m_out;
    QTextStream &m_err;
    QNetworkAccessManager m_network;
    QUrl m_api;
    PackageSystem m_system;
    std::optional<QString> m_installed;
    Runner m_runner;
    bool m_devel;
    Options m_options;
    QVersionNumber m_current;
    Done m_done;
    std::unique_ptr<QTemporaryDir> m_dir;
};

} // namespace callie::cli
