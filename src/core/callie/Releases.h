#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QSysInfo>
#include <QUrl>
#include <QVersionNumber>

#include <optional>

namespace callie {

/// A file attached to a release, with the SHA-256 GitHub records for it.
struct ReleaseAsset
{
    QString name;
    QUrl url;
    QString sha256;
};

/// One of Callie's published releases.
struct Release
{
    QVersionNumber version;
    QString tag;
    QUrl page;
    QList<ReleaseAsset> assets;
};

namespace releases {

/// GitHub's API for Callie's repository.
[[nodiscard]] QUrl defaultApi();
/// A release from GitHub's JSON, or nothing for a draft, a pre-release or a
/// tag that is not a version.
[[nodiscard]] std::optional<Release> parse(const QJsonObject &release);
/// The published releases in a list from GitHub, newest first.
[[nodiscard]] QList<Release> parseList(const QJsonArray &releases);

} // namespace releases

/// The packages this system takes, read from os-release: Callie ships them
/// for Fedora 44 or later, Debian 13 or later and Ubuntu 26.04 or later, on x86_64.
struct PackageSystem
{
    enum class Kind { Unsupported, Rpm, Deb };

    Kind kind = Kind::Unsupported;
    /// Ubuntu, whose debs are built apart from Debian's.
    bool ubuntu = false;
    /// The system's own name, such as "Fedora Linux 44".
    QString name;
    /// Why there are no packages for it, when there are none.
    QString why;

    [[nodiscard]] static PackageSystem
    detect(const QString &osRelease = QStringLiteral("/etc/os-release"),
           const QString &architecture = QSysInfo::currentCpuArchitecture());
    /// This system's package among the release's files: not the source or
    /// debugging ones.
    [[nodiscard]] std::optional<ReleaseAsset> packageIn(const Release &release) const;
};

} // namespace callie
