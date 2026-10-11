#include "callie/Releases.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace callie {

namespace releases {

QUrl defaultApi()
{
    return QUrl(u"https://api.github.com/repos/callieapp/callie"_s);
}

std::optional<Release> parse(const QJsonObject &release)
{
    if (release[u"draft"].toBool() || release[u"prerelease"].toBool())
        return std::nullopt;
    Release result;
    result.tag = release[u"tag_name"].toString();
    qsizetype rest = 0;
    result.version = QVersionNumber::fromString(
        QStringView(result.tag).mid(result.tag.startsWith(u'v') ? 1 : 0), &rest);
    if (result.version.isNull())
        return std::nullopt;
    result.page = QUrl(release[u"html_url"].toString());
    for (const QJsonValue &asset : release[u"assets"].toArray()) {
        const QString digest = asset[u"digest"].toString();
        result.assets.append({asset[u"name"].toString(),
                              QUrl(asset[u"browser_download_url"].toString()),
                              digest.startsWith(u"sha256:") ? digest.mid(7) : QString()});
    }
    return result;
}

QList<Release> parseList(const QJsonArray &releases)
{
    QList<Release> result;
    for (const QJsonValue &release : releases) {
        if (std::optional<Release> parsed = parse(release.toObject()))
            result.append(*parsed);
    }
    std::sort(result.begin(), result.end(),
              [](const Release &a, const Release &b) { return a.version > b.version; });
    return result;
}

} // namespace releases

PackageSystem PackageSystem::detect(const QString &osRelease, const QString &architecture)
{
    PackageSystem system;
    QFile file(osRelease);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        system.why = QObject::tr("cannot tell which Linux this is: %1 is missing").arg(osRelease);
        return system;
    }
    QHash<QString, QString> fields;
    for (const QByteArray &line : file.readAll().split('\n')) {
        const qsizetype equals = line.indexOf('=');
        if (equals <= 0)
            continue;
        QString value = QString::fromUtf8(line.mid(equals + 1)).trimmed();
        if (value.size() >= 2 && (value.startsWith(u'"') || value.startsWith(u'\'')))
            value = value.mid(1, value.size() - 2);
        fields.insert(QString::fromUtf8(line.left(equals)).trimmed(), value);
    }
    const QString id = fields.value(u"ID"_s);
    system.name = fields.value(u"PRETTY_NAME"_s, id);
    bool numeric = false;
    const int major = fields.value(u"VERSION_ID"_s).section(u'.', 0, 0).toInt(&numeric);
    const bool versioned = fields.contains(u"VERSION_ID"_s);

    if (architecture != u"x86_64") {
        system.why = QObject::tr("there are no packages for %1 yet").arg(architecture);
    } else if (id == u"fedora") {
        if (!numeric)
            system.why = QObject::tr("cannot read the Fedora version of %1").arg(system.name);
        else if (major < 44)
            system.why = QObject::tr("%1 is older than Fedora 44").arg(system.name);
        else
            system.kind = Kind::Rpm;
    } else if (id == u"debian") {
        // Testing and unstable have no version; they are newer than trixie.
        if (versioned && !numeric)
            system.why = QObject::tr("cannot read the Debian version of %1").arg(system.name);
        else if (versioned && major < 13)
            system.why = QObject::tr("%1 is older than Debian 13 (trixie)").arg(system.name);
        else
            system.kind = Kind::Deb;
    } else if (id == u"ubuntu") {
        system.why = QObject::tr("%1 is not supported yet: Callie needs Qt 6.8, newer than "
                                 "Ubuntu's LTS ships")
                         .arg(system.name);
    } else {
        system.why = QObject::tr("%1 is not one Callie ships packages for").arg(system.name);
    }
    return system;
}

std::optional<ReleaseAsset> PackageSystem::packageIn(const Release &release) const
{
    static const QRegularExpression rpm(u"^callie-[0-9][^/]*\\.x86_64\\.rpm$"_s);
    static const QRegularExpression deb(u"^callie_[0-9][^/]*_amd64\\.deb$"_s);
    const QRegularExpression *pattern = kind == Kind::Rpm   ? &rpm
                                        : kind == Kind::Deb ? &deb
                                                            : nullptr;
    if (!pattern)
        return std::nullopt;
    for (const ReleaseAsset &asset : release.assets) {
        if (pattern->match(asset.name).hasMatch())
            return asset;
    }
    return std::nullopt;
}

} // namespace callie
