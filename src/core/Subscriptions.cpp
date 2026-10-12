#include "callie/Subscriptions.h"

#include "callie/Identity.h"
#include "callie/Logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

const QString kStateFile = u".callie-state.json"_s;
constexpr qint64 kLargestFeed = 32 * 1024 * 1024;

QString readText(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

bool writeText(const QString &path, const QString &text)
{
    QSaveFile file(path);
    const QByteArray data = text.toUtf8() + '\n';
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

/// The name a feed gives itself, if any.
QString feedName(const QByteArray &feed)
{
    static const QRegularExpression line(u"^X-WR-CALNAME:(.*)$"_s,
                                         QRegularExpression::MultilineOption);
    QString name = line.match(QString::fromUtf8(feed)).captured(1).trimmed();
    name.replace(u"\\,"_s, u","_s).replace(u"\\;"_s, u";"_s).replace(u"\\n"_s, u" "_s);
    return name.simplified();
}

} // namespace

Subscriptions::Subscriptions(QString root, QNetworkAccessManager *network, QObject *parent)
    : VdirSource(parent), m_root(std::move(root)), m_network(network)
{
    scan();
}

QString Subscriptions::defaultRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u'/' +
           identity::dirName() + u"/subscriptions"_s;
}

QUrl Subscriptions::feedUrl(const QString &link)
{
    QUrl url(link.trimmed());
    const QString scheme = url.scheme().toLower();
    if (scheme == u"webcal" || scheme == u"webcals")
        url.setScheme(u"https"_s);
    else if (scheme != u"https" && scheme != u"http")
        return {};
    return url.host().isEmpty() ? QUrl() : url;
}

void Subscriptions::scan()
{
    QList<Folder> folders;
    const QDir root(m_root);
    for (const QString &name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        folders.append({u"subscription:"_s + name, root.filePath(name), false, QString(kAccount)});
    const auto same = [](const Folder &a, const Folder &b) { return a.id == b.id; };
    if (!std::ranges::equal(folders, this->folders(), same))
        setFolders(folders);
}

QString Subscriptions::link(const QString &id) const
{
    for (const Folder &folder : folders()) {
        if (folder.id == id)
            return readText(folder.path + u"/url"_s);
    }
    return {};
}

QString Subscriptions::add(const QString &link, const QString &name, QString *error)
{
    const QUrl url = feedUrl(link);
    if (!url.isValid()) {
        *error = tr("%1 is not a calendar link.").arg(link);
        return {};
    }
    for (const Folder &folder : folders()) {
        if (feedUrl(readText(folder.path + u"/url"_s)) == url)
            return folder.id;
    }
    QString base = (name.simplified().isEmpty() ? url.host() : name.simplified()).toLower();
    base.replace(QRegularExpression(u"[^\\w-]+"_s), u"-"_s);
    base.remove(QRegularExpression(u"^[-.]+|-+$"_s));
    if (base.isEmpty())
        base = u"calendar"_s;
    const QDir root(m_root);
    QString folderName = base;
    for (int n = 2; root.exists(folderName); ++n)
        folderName = base + u'-' + QString::number(n);
    const QString path = root.filePath(folderName);
    if (!QDir().mkpath(path) || !writeText(path + u"/url"_s, link.trimmed()) ||
        (!name.simplified().isEmpty() && !writeText(path + u"/displayname"_s, name.simplified()))) {
        *error = tr("Callie could not create %1.").arg(path);
        return {};
    }
    scan();
    const QString id = u"subscription:"_s + folderName;
    for (const Folder &folder : folders()) {
        if (folder.id == id)
            fetch(folder);
    }
    return id;
}

bool Subscriptions::remove(const QString &id, QString *error)
{
    for (const Folder &folder : folders()) {
        if (folder.id != id)
            continue;
        // A copy of someone else's calendar, so nothing of the user's is lost.
        if (!QDir(folder.path).removeRecursively()) {
            *error = tr("Callie could not delete %1.").arg(folder.path);
            return false;
        }
        scan();
        Q_EMIT statusChanged();
        return true;
    }
    *error = tr("There is no subscription %1.").arg(id);
    return false;
}

void Subscriptions::refresh()
{
    scan();
    const QDateTime due = QDateTime::currentDateTimeUtc().addSecs(
        -std::chrono::duration_cast<std::chrono::seconds>(kEvery).count());
    for (const Folder &folder : folders()) {
        const State state = readState(folder.path);
        if (!state.fetched.isValid() || state.fetched < due)
            fetch(folder);
    }
    VdirSource::refresh();
}

void Subscriptions::fetchAll()
{
    scan();
    for (const Folder &folder : folders())
        fetch(folder);
}

void Subscriptions::fetch(const Folder &folder)
{
    if (!m_network || m_fetching.contains(folder.id))
        return;
    const QUrl url = feedUrl(readText(folder.path + u"/url"_s));
    State state = readState(folder.path);
    if (!url.isValid()) {
        state.error = tr("the link is missing");
        writeState(folder.path, state);
        return;
    }
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      u"Callie/"_s + QCoreApplication::applicationVersion());
    request.setTransferTimeout(30000);
    if (!state.etag.isEmpty())
        request.setRawHeader("If-None-Match", state.etag.toUtf8());
    if (!state.modified.isEmpty())
        request.setRawHeader("If-Modified-Since", state.modified.toUtf8());
    m_fetching.insert(folder.id);
    Q_EMIT statusChanged();
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
        if (received > kLargestFeed)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, folder, state]() mutable {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        state.fetched = QDateTime::currentDateTimeUtc();
        state.error.clear();
        if (status == 304) {
            finish(folder, state);
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            state.error = reply->errorString();
            finish(folder, state);
            return;
        }
        const QByteArray feed = reply->readAll();
        QString problem;
        const ics::Group events = ics::parse(feed, &problem);
        if (!problem.isEmpty()) {
            state.error = tr("the link does not lead to a calendar");
            finish(folder, state);
            return;
        }
        if (!mirror(folder, events, &problem)) {
            state.error = problem;
            finish(folder, state);
            return;
        }
        state.etag = QString::fromUtf8(reply->rawHeader("ETag"));
        state.modified = QString::fromUtf8(reply->rawHeader("Last-Modified"));
        // Named by the feed unless the user gave it a name.
        if (const QString name = feedName(feed);
            !name.isEmpty() && !QFile::exists(folder.path + u"/displayname"_s))
            writeText(folder.path + u"/displayname"_s, name);
        finish(folder, state);
    });
}

void Subscriptions::finish(const Folder &folder, const State &state)
{
    if (!state.error.isEmpty())
        qCInfo(lcSync) << "subscription" << folder.id << "not fetched:" << state.error;
    writeState(folder.path, state);
    m_fetching.remove(folder.id);
    Q_EMIT changed();
    Q_EMIT statusChanged();
    if (m_fetching.isEmpty())
        Q_EMIT fetched();
}

Subscriptions::State Subscriptions::readState(const QString &path)
{
    QFile file(path + u'/' + kStateFile);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
    return {json[u"etag"].toString(), json[u"modified"].toString(),
            QDateTime::fromString(json[u"fetched"].toString(), Qt::ISODate),
            json[u"error"].toString()};
}

void Subscriptions::writeState(const QString &path, const State &state)
{
    QSaveFile file(path + u'/' + kStateFile);
    const QByteArray data =
        QJsonDocument(QJsonObject{{u"etag"_s, state.etag},
                                  {u"modified"_s, state.modified},
                                  {u"fetched"_s, state.fetched.toString(Qt::ISODate)},
                                  {u"error"_s, state.error}})
            .toJson();
    if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size())
        file.commit();
}

QDateTime Subscriptions::lastSynced() const
{
    QDateTime oldest;
    for (const Folder &folder : folders()) {
        const QDateTime fetched = readState(folder.path).fetched;
        if (fetched.isValid() && (!oldest.isValid() || fetched < oldest))
            oldest = fetched;
    }
    return oldest;
}

QString Subscriptions::lastError() const
{
    const QList<CalendarInfo> all = calendars();
    for (qsizetype i = 0; i < all.size(); ++i) {
        if (const QString error = readState(folders().at(i).path).error; !error.isEmpty())
            return u"%1: %2"_s.arg(all.at(i).displayName, error);
    }
    return {};
}

QVariantList Subscriptions::syncReport() const
{
    QVariantList report;
    const QList<CalendarInfo> all = calendars();
    for (qsizetype i = 0; i < all.size(); ++i) {
        const State state = readState(folders().at(i).path);
        report << QVariantMap{{u"account"_s, all.at(i).displayName},
                              {u"lastSynced"_s, state.fetched.toLocalTime()},
                              {u"error"_s, state.error},
                              {u"problems"_s, QStringList()}};
    }
    return report;
}

} // namespace callie
