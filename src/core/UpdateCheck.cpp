#include "callie/UpdateCheck.h"

#include "callie/Identity.h"
#include "callie/Logging.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSaveFile>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace callie {

UpdateCheck::UpdateCheck(QString path, QObject *parent)
    : QObject(parent), m_path(std::move(path)), m_api(releases::defaultApi()),
      m_now([] { return QDateTime::currentDateTimeUtc(); }), m_devel(identity::isDevel())
{}

QString UpdateCheck::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + u'/' +
           identity::dirName() + u"/latest-release.json"_s;
}

bool UpdateCheck::wanted(bool setting) const
{
    return setting && !m_devel && qEnvironmentVariableIsEmpty("CALLIE_NO_UPDATE_CHECK");
}

QJsonObject UpdateCheck::read() const
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

void UpdateCheck::remember(const QJsonObject &entry)
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(entry).toJson(QJsonDocument::Compact)) < 0 || !file.commit())
        qCInfo(lcUi) << "could not remember the latest release:" << file.errorString();
}

std::optional<Release> UpdateCheck::newerThan(const QVersionNumber &running) const
{
    const QJsonObject entry = read();
    Release latest;
    latest.version = QVersionNumber::fromString(entry[u"version"].toString());
    latest.tag = u'v' + latest.version.toString();
    latest.page = QUrl(entry[u"page"].toString());
    if (latest.version.isNull() || latest.version <= running)
        return std::nullopt;
    return latest;
}

bool UpdateCheck::due() const
{
    const QDateTime asked = QDateTime::fromString(read()[u"asked"].toString(), Qt::ISODateWithMs);
    return !asked.isValid() || asked.secsTo(m_now()) >= 24 * 3600 || asked > m_now();
}

void UpdateCheck::markAsked()
{
    QJsonObject entry = read();
    entry[u"asked"_s] = m_now().toString(Qt::ISODateWithMs);
    remember(entry);
}

void UpdateCheck::check()
{
    markAsked();
    QNetworkRequest request(QUrl(m_api.toString() + u"/releases/latest"_s));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(15000);
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const std::optional<Release> latest =
            reply->error() == QNetworkReply::NoError
                ? releases::parse(QJsonDocument::fromJson(reply->readAll()).object())
                : std::nullopt;
        if (latest) {
            QJsonObject entry = read();
            entry[u"version"_s] = latest->version.toString();
            entry[u"page"_s] = latest->page.toString();
            remember(entry);
        } else {
            qCInfo(lcUi) << "could not learn the latest release:" << reply->errorString();
        }
        Q_EMIT checked();
    });
}

} // namespace callie
