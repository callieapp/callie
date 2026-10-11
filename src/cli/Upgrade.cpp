#include "Upgrade.h"

#include "callie/Identity.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QProcess>

#include <unistd.h>

using namespace Qt::StringLiterals;

namespace callie::cli {

namespace {

const QString kInstaller = u"curl -fsSL https://callieapp.org/install.sh | sh"_s;

int runWithTerminal(const QStringList &command)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::ForwardedChannels);
    process.setInputChannelMode(QProcess::ForwardedInputChannel);
    process.start(command.first(), command.mid(1));
    if (!process.waitForStarted())
        return -1;
    process.waitForFinished(-1);
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
}

/// The release part of a package version, without its packaging revision.
QString releaseOf(const QString &packageVersion)
{
    return packageVersion.section(u'-', 0, 0).trimmed();
}

} // namespace

Upgrade::Upgrade(QTextStream &out, QTextStream &err, QObject *parent)
    : QObject(parent), m_out(out), m_err(err), m_api(releases::defaultApi()),
      m_system(PackageSystem::detect()), m_runner(runWithTerminal), m_devel(identity::isDevel())
{}

void Upgrade::run(const Options &options, Done done)
{
    m_options = options;
    m_done = std::move(done);
    if (m_devel) {
        m_err << tr("callie: this is a development build; update it with git pull and make")
              << "\n";
        return finish(1);
    }
    if (m_system.kind == PackageSystem::Kind::Unsupported) {
        m_err << tr("callie: %1, so there is no package to install.").arg(m_system.why) << "\n";
        return finish(1);
    }
    const QString installed = installedVersion();
    m_current = QVersionNumber::fromString(installed);
    if (m_current.isNull()) {
        m_err << tr("callie: Callie is not installed from a package here. Install it with:") << "\n"
              << "  " << kInstaller << "\n";
        return finish(1);
    }

    if (m_options.version) {
        const QString tag = u'v' + (m_options.version->startsWith(u'v') ? m_options.version->mid(1)
                                                                        : *m_options.version);
        fetch(QUrl(m_api.toString() + u"/releases/tags/"_s + tag), tag, [this](QByteArray body) {
            const std::optional<Release> release =
                releases::parse(QJsonDocument::fromJson(body).object());
            chooseFrom(release ? QList<Release>{*release} : QList<Release>{});
        });
    } else if (m_options.downgrade) {
        fetch(QUrl(m_api.toString() + u"/releases?per_page=100"_s), tr("the releases"),
              [this](QByteArray body) {
                  chooseFrom(releases::parseList(QJsonDocument::fromJson(body).array()));
              });
    } else {
        fetch(QUrl(m_api.toString() + u"/releases/latest"_s), tr("the latest release"),
              [this](QByteArray body) {
                  const std::optional<Release> release =
                      releases::parse(QJsonDocument::fromJson(body).object());
                  chooseFrom(release ? QList<Release>{*release} : QList<Release>{});
              });
    }
}

void Upgrade::chooseFrom(const QList<Release> &found)
{
    const QString current = m_current.toString();
    std::optional<Release> target;
    if (m_options.downgrade && !m_options.version) {
        for (const Release &release : found) {
            if (release.version < m_current) {
                target = release;
                break;
            }
        }
        if (!target) {
            m_err << tr("callie: there is no release older than %1.").arg(current) << "\n";
            return finish(1);
        }
    } else if (!found.isEmpty()) {
        target = found.first();
    } else {
        m_err << tr("callie: GitHub has no such release.") << "\n";
        return finish(1);
    }

    const QString version = target->version.toString();
    if (target->version == m_current && !m_options.force) {
        m_out << tr("Callie %1 is installed and up to date.").arg(current) << "\n";
        return finish(0);
    }
    if (target->version > m_current && m_options.downgrade) {
        m_err << tr("callie: %1 is newer than the installed %2; run: callie upgrade %1")
                     .arg(version, current)
              << "\n";
        return finish(2);
    }
    if (target->version < m_current && !m_options.downgrade) {
        if (m_options.version) {
            m_err << tr("callie: %1 is older than the installed %2; run: callie downgrade %1")
                         .arg(version, current)
                  << "\n";
            return finish(2);
        }
        m_out << tr("Callie %1 is installed, newer than the latest release, %2.")
                     .arg(current, version)
              << "\n";
        return finish(0);
    }
    install(*target);
}

void Upgrade::install(const Release &release)
{
    const std::optional<ReleaseAsset> package = m_system.packageIn(release);
    if (!package) {
        m_err << tr("callie: release %1 has no package for %2.").arg(release.tag, m_system.name)
              << "\n";
        return finish(1);
    }
    if (package->sha256.isEmpty()) {
        m_err << tr("callie: GitHub gave no checksum for %1, so it cannot be checked.")
                     .arg(package->name)
              << "\n";
        return finish(1);
    }

    const QString version = release.version.toString();
    const QString current = m_current.toString();
    const bool rpm = m_system.kind == PackageSystem::Kind::Rpm;
    if (release.version == m_current)
        m_out << tr("Installing Callie %1 again.").arg(version) << "\n";
    else if (release.version < m_current)
        m_out << tr("Going back from Callie %1 to %2.").arg(current, version) << "\n";
    else
        m_out << tr("Updating Callie %1 to %2.").arg(current, version) << "\n";

    m_dir = std::make_unique<QTemporaryDir>();
    const QString file = m_dir->filePath(package->name);
    const QStringList sudo = ::geteuid() == 0 ? QStringList() : QStringList{u"sudo"_s};
    QList<QStringList> commands;
    if (rpm) {
        const QString verb = release.version == m_current  ? u"reinstall"_s
                             : release.version < m_current ? u"downgrade"_s
                                                           : u"install"_s;
        commands.append(sudo + QStringList{u"dnf"_s, verb, u"-y"_s, file});
    } else {
        // apt resolves dependencies from its lists, which may be stale.
        commands.append(sudo + QStringList{u"apt-get"_s, u"update"_s});
        QStringList install = sudo + QStringList{u"apt-get"_s, u"install"_s, u"-y"_s};
        if (release.version == m_current)
            install << u"--reinstall"_s;
        else if (release.version < m_current)
            install << u"--allow-downgrades"_s;
        commands.append(install << file);
    }

    if (m_options.dryRun) {
        m_out << tr("Would download %1").arg(package->url.toString()) << "\n"
              << tr("check it against sha256 %1").arg(package->sha256) << "\n";
        for (const QStringList &command : std::as_const(commands))
            m_out << tr("and run: %1").arg(command.join(u' ')) << "\n";
        return finish(0);
    }

    m_out << tr("Downloading %1").arg(package->name) << "\n" << Qt::flush;
    fetch(package->url, package->name, [this, package, file, commands, version](QByteArray body) {
        if (QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex() !=
            package->sha256.toLatin1()) {
            m_err << tr("callie: %1 does not match its checksum, so it was not installed.")
                         .arg(package->name)
                  << "\n";
            return finish(1);
        }
        QFile saved(file);
        if (!saved.open(QIODevice::WriteOnly) || saved.write(body) != body.size()) {
            m_err << tr("callie: could not save %1: %2").arg(file, saved.errorString()) << "\n";
            return finish(1);
        }
        saved.close();
        m_out << Qt::flush;
        for (const QStringList &command : commands) {
            if (m_runner(command) != 0) {
                m_err << tr("callie: this did not work: %1").arg(command.join(u' ')) << "\n";
                return finish(1);
            }
        }
        m_out << tr("Callie %1 is installed. If the app is open, quit it and open it again to "
                    "use it.")
                     .arg(version)
              << "\n";
        finish(0);
    });
}

QString Upgrade::installedVersion()
{
    if (m_installed)
        return releaseOf(*m_installed);
    QProcess query;
    if (m_system.kind == PackageSystem::Kind::Rpm)
        query.start(u"rpm"_s, {u"-q"_s, u"--qf"_s, u"%{VERSION}"_s, u"callie"_s});
    else
        query.start(u"dpkg-query"_s, {u"-W"_s, u"-f=${Status}|${Version}"_s, u"callie"_s});
    if (!query.waitForFinished() || query.exitStatus() != QProcess::NormalExit ||
        query.exitCode() != 0)
        return {};
    const QString output = QString::fromUtf8(query.readAllStandardOutput());
    if (m_system.kind == PackageSystem::Kind::Rpm)
        return releaseOf(output);
    return output.startsWith(u"install ok installed|") ? releaseOf(output.section(u'|', 1))
                                                       : QString();
}

void Upgrade::fetch(const QUrl &url, const QString &what, std::function<void(QByteArray)> then)
{
    QNetworkRequest request(url);
    // GitHub's API answers in JSON; the package itself comes as it is.
    if (url.toString().startsWith(m_api.toString()))
        request.setRawHeader("Accept", "application/vnd.github+json");
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      u"callie/"_s + QCoreApplication::applicationVersion());
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, what, then = std::move(then)] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 404)
                m_err << tr("callie: GitHub has no %1.").arg(what) << "\n";
            else
                m_err << tr("callie: could not get %1 from GitHub: %2")
                             .arg(what, reply->errorString())
                      << "\n";
            return finish(1);
        }
        then(reply->readAll());
    });
}

void Upgrade::finish(int code)
{
    m_out << Qt::flush;
    m_err << Qt::flush;
    if (m_done)
        std::exchange(m_done, nullptr)(code);
}

} // namespace callie::cli
