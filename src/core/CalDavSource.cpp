#include "callie/CalDavSource.h"

#include "callie/Identity.h"
#include "callie/Logging.h"
#include "callie/TokenStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

const QString kStateFile = u".callie-dav.json"_s;
const QString kAccountFile = u".callie-account.json"_s;

/// A folder name from any text: lower case, with runs of other characters as dashes.
QString slug(const QString &text)
{
    QString name = text.toLower();
    name.replace(QRegularExpression(u"[^\\w.@-]+"_s), u"-"_s);
    name.remove(QRegularExpression(u"^[-.]+|-+$"_s));
    return name.isEmpty() ? u"calendar"_s : name;
}

bool writeJson(const QString &path, const QJsonObject &json)
{
    QSaveFile file(path);
    const QByteArray data = QJsonDocument(json).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object()
                                          : QJsonObject();
}

void writeMeta(const QString &path, const QString &value)
{
    QSaveFile file(path);
    const QByteArray data = value.toUtf8() + '\n';
    if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size())
        file.commit();
}

/// A file name for an event the server keeps at `href`: the last part of its
/// address, when that is a safe name.
QString fileNameOf(const QUrl &href)
{
    const QString last = href.path(QUrl::FullyDecoded).section(u'/', -1);
    static const QRegularExpression safe(u"^[A-Za-z0-9._@-]{1,200}$"_s);
    if (safe.match(last).hasMatch() && !last.startsWith(u'.'))
        return last.endsWith(u".ics"_s) ? last : last + u".ics"_s;
    return QString::fromLatin1(
               QCryptographicHash::hash(href.path().toUtf8(), QCryptographicHash::Sha1).toHex()) +
           u".ics"_s;
}

} // namespace

CalDavSource::CalDavSource(QString root, QNetworkAccessManager *network, TokenStore *tokens,
                           QObject *parent)
    : VdirSource(parent), m_root(std::move(root)), m_network(network), m_tokens(tokens)
{
    m_soon.setSingleShot(true);
    m_soon.setInterval(500);
    connect(&m_soon, &QTimer::timeout, this, &CalDavSource::refresh);
}

CalDavSource::~CalDavSource() = default;

QString CalDavSource::defaultRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u'/' +
           identity::dirName() + u"/caldav"_s;
}

QString CalDavSource::accountPath(const QString &accountId) const
{
    return m_root + u'/' + slug(accountId);
}

void CalDavSource::setAccounts(const QList<Account> &accounts)
{
    m_accounts = accounts;
    for (auto it = m_clients.begin(); it != m_clients.end();) {
        const bool kept = std::ranges::any_of(
            m_accounts, [&it](const Account &account) { return account.id == it.key(); });
        if (!kept) {
            it.value()->deleteLater();
            it = m_clients.erase(it);
        } else {
            ++it;
        }
    }
    scan();
}

void CalDavSource::scan()
{
    QList<Folder> folders;
    for (const Account &account : std::as_const(m_accounts)) {
        const QString path = accountPath(account.id);
        const AccountState state = readAccount(path);
        const QString self = !state.addresses.isEmpty()    ? state.addresses.first()
                             : account.user.contains(u'@') ? account.user
                                                           : QString();
        const QDir dir(path);
        for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString folder = dir.filePath(name);
            folders.append({u"caldav:"_s + slug(account.id) + u'/' + name, folder,
                            readState(folder).writable, account.id, self});
        }
    }
    const auto same = [](const Folder &a, const Folder &b) {
        return a.id == b.id && a.writable == b.writable && a.self == b.self;
    };
    if (!std::ranges::equal(folders, this->folders(), same))
        setFolders(folders);
}

bool CalDavSource::forget(const QString &accountId, QString *error)
{
    const QString path = accountPath(accountId);
    // A copy of what the server keeps, so nothing is lost but changes not sent.
    if (QDir(path).exists() && !QDir(path).removeRecursively()) {
        *error = tr("Callie could not delete %1.").arg(path);
        return false;
    }
    m_accounts.removeIf([&accountId](const Account &account) { return account.id == accountId; });
    if (CalDavClient *client = m_clients.take(accountId))
        client->deleteLater();
    scan();
    Q_EMIT statusChanged();
    return true;
}

void CalDavSource::refresh()
{
    scan();
    for (const Account &account : std::as_const(m_accounts))
        sync(account);
    VdirSource::refresh();
}

void CalDavSource::fileChanged(const Folder &folder, const QString &fileName, bool removed)
{
    State state = readState(folder.path);
    state.pending.insert(fileName, removed);
    writeState(folder.path, state);
    m_soon.start();
    Q_EMIT statusChanged();
}

void CalDavSource::sync(const Account &account)
{
    if (m_running.contains(account.id)) {
        m_again.insert(account.id);
        return;
    }
    if (!m_network || !m_tokens)
        return;
    m_running.insert(account.id);
    Q_EMIT statusChanged();
    const QPointer<CalDavSource> self(this);
    m_tokens->read({u"caldav"_s, account.id}, [self, account](const QString &password,
                                                              const QString &keyring) {
        if (!self)
            return;
        if (!keyring.isEmpty() || password.isEmpty()) {
            self->finish(account, keyring.isEmpty()
                                      ? tr("No password is stored for this account; sign in again.")
                                      : keyring);
            return;
        }
        CalDavClient *&client = self->m_clients[account.id];
        if (!client)
            client = new CalDavClient(self->m_network, self);
        client->setCredentials(account.user, password);
        const QString path = self->accountPath(account.id);
        AccountState state = readAccount(path);
        const auto listed = [self, account](const QList<DavCalendar> &calendars,
                                            const DavError &error) {
            if (!self)
                return;
            if (error) {
                self->finish(account, error.message);
                return;
            }
            self->updateCalendars(account, calendars);
            QList<Folder> mine;
            for (const Folder &folder : self->folders()) {
                if (folder.account == account.id)
                    mine.append(folder);
            }
            self->syncFolders(account, mine, {});
        };
        if (state.home.isValid()) {
            client->calendars(state.home, listed);
            return;
        }
        client->discover(account.server, [self, account, path, listed](const DavAccount &found,
                                                                       const DavError &error) {
            if (!self)
                return;
            if (error) {
                self->finish(account, error.message);
                return;
            }
            AccountState state = readAccount(path);
            state.home = found.home;
            state.addresses = found.addresses;
            QDir().mkpath(path);
            writeAccount(path, state);
            listed(found.calendars, {});
        });
    });
}

void CalDavSource::updateCalendars(const Account &account, const QList<DavCalendar> &calendars)
{
    const QString path = accountPath(account.id);
    const QDir dir(path);
    QHash<QString, QString> byUrl;
    for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        byUrl.insert(readState(dir.filePath(name)).url.toString(), dir.filePath(name));
    QSet<QString> seen;
    for (const DavCalendar &calendar : calendars) {
        QString folder = byUrl.value(calendar.url.toString());
        if (folder.isEmpty()) {
            const QString base = slug(calendar.url.path(QUrl::FullyDecoded).section(u'/', -2, -2));
            QString name = base;
            for (int n = 2; dir.exists(name); ++n)
                name = base + u'-' + QString::number(n);
            folder = dir.filePath(name);
            QDir().mkpath(folder);
        }
        seen.insert(folder);
        State state = readState(folder);
        state.url = calendar.url;
        state.writable = calendar.writable;
        writeState(folder, state);
        writeMeta(folder + u"/displayname"_s,
                  calendar.displayName.isEmpty() ? QDir(folder).dirName() : calendar.displayName);
        if (calendar.color.isValid())
            writeMeta(folder + u"/color"_s, calendar.color.name());
    }
    // Calendars gone from the server go from here too.
    for (const QString &folder : std::as_const(byUrl)) {
        if (!seen.contains(folder))
            QDir(folder).removeRecursively();
    }
    scan();
}

void CalDavSource::syncFolders(const Account &account, QList<Folder> folders, QStringList errors)
{
    CalDavClient *client = m_clients.value(account.id);
    if (folders.isEmpty() || !client) {
        finish(account, errors.join(u'\n'));
        return;
    }
    const Folder folder = folders.takeFirst();
    const QPointer<CalDavSource> self(this);
    push(client, folder, [self, client, account, folder, folders, errors](const QString &pushed) {
        if (!self)
            return;
        self->pull(client, folder,
                   [self, account, folders, errors, pushed](const QString &pulled) mutable {
                       if (!self)
                           return;
                       for (const QString &error : {pushed, pulled}) {
                           if (!error.isEmpty() && !errors.contains(error))
                               errors << error;
                       }
                       self->syncFolders(account, folders, errors);
                   });
    });
}

void CalDavSource::push(CalDavClient *client, const Folder &folder, Done done)
{
    State state = readState(folder.path);
    if (state.pending.isEmpty()) {
        done({});
        return;
    }
    const QString fileName = state.pending.keys().first();
    const bool removed = state.pending.value(fileName);
    const Remote remote = state.files.value(fileName);
    const QPointer<CalDavSource> self(this);
    // Sent, or settled another way: on to the next.
    const auto next = [self, client, folder, fileName, done](const Remote &now, bool gone) {
        if (!self)
            return;
        State state = readState(folder.path);
        state.pending.remove(fileName);
        if (gone)
            state.files.remove(fileName);
        else
            state.files.insert(fileName, now);
        writeState(folder.path, state);
        self->push(client, folder, done);
    };
    // Turned down by the server: its copy stands, and the user hears why.
    const auto refused = [self, client, folder, fileName, remote, done](const DavError &error) {
        if (!self)
            return;
        State state = readState(folder.path);
        state.pending.remove(fileName);
        writeState(folder.path, state);
        Q_EMIT self->errorOccurred(error.isConflict()
                                       ? tr("An event was changed elsewhere at the same time, "
                                            "so Callie kept that version.")
                                       : error.message);
        self->restore(client, folder, fileName, remote.href,
                      [self, client, folder, done](const QString &) {
                          if (self)
                              self->push(client, folder, done);
                      });
    };
    if (removed) {
        if (!remote.href.isValid()) {
            next({}, true);
            return;
        }
        client->remove(remote.href, remote.etag, [next, refused, done](const DavError &error) {
            if (!error)
                next({}, true);
            else if (error.status == 0 || error.status >= 500)
                done(error.message); // Kept for next time.
            else
                refused(error);
        });
        return;
    }
    QFile file(folder.path + u'/' + fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        next(remote, false);
        return;
    }
    // New events go inside the calendar, whose address may lack its last slash.
    QUrl calendar = state.url;
    if (!calendar.path().endsWith(u'/'))
        calendar.setPath(calendar.path() + u'/');
    const QUrl href = remote.href.isValid() ? remote.href
                                            : calendar.resolved(QUrl(QString::fromLatin1(
                                                  QUrl::toPercentEncoding(fileName, "@._-"))));
    client->put(href, file.readAll(), remote.etag,
                [next, refused, done, href](const QString &etag, const DavError &error) {
                    if (!error)
                        next({href, etag}, false);
                    else if (error.status == 0 || error.status >= 500)
                        done(error.message);
                    else
                        refused(error);
                });
}

void CalDavSource::restore(CalDavClient *client, const Folder &folder, const QString &fileName,
                           const QUrl &href, Done done)
{
    const QPointer<CalDavSource> self(this);
    const auto gone = [self, folder, fileName, done] {
        if (!self)
            return;
        QString error;
        self->removeFile(folder, fileName, &error);
        State state = readState(folder.path);
        state.files.remove(fileName);
        writeState(folder.path, state);
        Q_EMIT self->changed();
        done(error);
    };
    if (!href.isValid()) {
        gone();
        return;
    }
    client->fetch(readState(folder.path).url, {href},
                  [self, folder, fileName, href, done, gone](const QList<DavItem> &items,
                                                             const DavError &error) {
                      if (!self)
                          return;
                      if (error) {
                          done(error.message);
                          return;
                      }
                      if (items.isEmpty()) {
                          gone();
                          return;
                      }
                      QString problem;
                      self->writeFile(folder, fileName, items.first().data, &problem);
                      State state = readState(folder.path);
                      state.files.insert(fileName, {href, items.first().etag});
                      writeState(folder.path, state);
                      Q_EMIT self->changed();
                      done(problem);
                  });
}

void CalDavSource::pull(CalDavClient *client, const Folder &folder, Done done)
{
    const State state = readState(folder.path);
    const QPointer<CalDavSource> self(this);
    // For servers without sync tokens: what differs from the versions kept.
    const auto byVersions = [self, client, folder, done] {
        const State state = readState(folder.path);
        client->list(state.url, [self, client, folder, done](const QList<DavItem> &items,
                                                             const DavError &error) {
            if (!self)
                return;
            if (error) {
                done(error.message);
                return;
            }
            const State state = readState(folder.path);
            QHash<QString, QString> kept;
            for (const Remote &remote : state.files)
                kept.insert(remote.href.toString(), remote.etag);
            QList<DavItem> changed;
            QSet<QString> there;
            for (const DavItem &item : items) {
                there.insert(item.href.toString());
                if (kept.value(item.href.toString()) != item.etag || item.etag.isEmpty())
                    changed << item;
            }
            QList<QUrl> removed;
            for (const Remote &remote : state.files) {
                if (!there.contains(remote.href.toString()))
                    removed << remote.href;
            }
            self->apply(client, folder, changed, removed, {}, done);
        });
    };
    client->changes(state.url, state.syncToken,
                    [self, client, folder, done, byVersions, token = state.syncToken](
                        const CalDavClient::Changes &changes, const DavError &error) {
                        if (!self)
                            return;
                        if (!error) {
                            self->apply(client, folder, changes.changed, changes.removed,
                                        changes.token, done);
                            return;
                        }
                        if (error.status == 0 || error.isAuth()) {
                            done(error.message);
                            return;
                        }
                        // A token the server has forgotten starts over; a server
                        // without tokens is compared by versions.
                        if (!token.isEmpty()) {
                            State state = readState(folder.path);
                            state.syncToken.clear();
                            writeState(folder.path, state);
                            self->pull(client, folder, done);
                            return;
                        }
                        byVersions();
                    });
}

void CalDavSource::apply(CalDavClient *client, const Folder &folder, const QList<DavItem> &changed,
                         const QList<QUrl> &removed, const QString &token, Done done)
{
    const State state = readState(folder.path);
    QHash<QString, QString> fileOf;
    for (auto it = state.files.cbegin(); it != state.files.cend(); ++it)
        fileOf.insert(it->href.toString(), it.key());
    // Changes not sent yet stay as they are until they go.
    const auto waiting = [&state, &fileOf](const QUrl &href) {
        return state.pending.contains(fileOf.value(href.toString(), fileNameOf(href)));
    };
    QList<QUrl> wanted;
    for (const DavItem &item : changed) {
        if (!waiting(item.href))
            wanted << item.href;
    }
    const QPointer<CalDavSource> self(this);
    client->fetch(state.url, wanted,
                  [self, folder, removed, token, done, fileOf](const QList<DavItem> &items,
                                                               const DavError &error) {
                      if (!self)
                          return;
                      if (error) {
                          done(error.message);
                          return;
                      }
                      State state = readState(folder.path);
                      QString problem;
                      for (const DavItem &item : items) {
                          const QString fileName =
                              fileOf.value(item.href.toString(), fileNameOf(item.href));
                          if (state.pending.contains(fileName))
                              continue;
                          if (self->writeFile(folder, fileName, item.data, &problem))
                              state.files.insert(fileName, {item.href, item.etag});
                      }
                      for (const QUrl &href : removed) {
                          const QString fileName = fileOf.value(href.toString());
                          if (fileName.isEmpty() || state.pending.contains(fileName))
                              continue;
                          if (self->removeFile(folder, fileName, &problem))
                              state.files.remove(fileName);
                      }
                      if (!token.isEmpty())
                          state.syncToken = token;
                      writeState(folder.path, state);
                      if (!items.isEmpty() || !removed.isEmpty())
                          Q_EMIT self->changed();
                      done(problem);
                  });
}

void CalDavSource::finish(const Account &account, const QString &error)
{
    const QString path = accountPath(account.id);
    AccountState state = readAccount(path);
    state.error = error;
    if (error.isEmpty())
        state.synced = QDateTime::currentDateTimeUtc();
    else
        qCInfo(lcSync) << "CalDAV" << account.id << "not synced:" << error;
    // Kept even before the first sync works, so the error has somewhere to live.
    if (std::ranges::any_of(m_accounts,
                            [&account](const Account &each) { return each.id == account.id; }) &&
        QDir().mkpath(path))
        writeAccount(path, state);
    m_running.remove(account.id);
    Q_EMIT statusChanged();
    if (m_again.remove(account.id)) {
        const auto again = std::ranges::find_if(
            m_accounts, [&account](const Account &each) { return each.id == account.id; });
        if (again != m_accounts.end())
            sync(*again);
    }
    if (m_running.isEmpty())
        Q_EMIT synced();
}

QDateTime CalDavSource::lastSynced() const
{
    QDateTime oldest;
    for (const Account &account : m_accounts) {
        const QDateTime synced = readAccount(accountPath(account.id)).synced;
        if (synced.isValid() && (!oldest.isValid() || synced < oldest))
            oldest = synced;
    }
    return oldest;
}

QString CalDavSource::lastError() const
{
    for (const Account &account : m_accounts) {
        if (const QString error = readAccount(accountPath(account.id)).error; !error.isEmpty())
            return u"%1: %2"_s.arg(account.id, error);
    }
    return {};
}

QVariantList CalDavSource::syncReport() const
{
    QVariantList report;
    for (const Account &account : m_accounts) {
        const AccountState state = readAccount(accountPath(account.id));
        report << QVariantMap{{u"account"_s, account.id},
                              {u"lastSynced"_s, state.synced.toLocalTime()},
                              {u"error"_s, state.error},
                              {u"problems"_s, QStringList()}};
    }
    return report;
}

QStringList CalDavSource::waitingChanges() const
{
    QStringList waiting;
    for (const Folder &folder : folders()) {
        const qsizetype count = readState(folder.path).pending.size();
        if (count > 0)
            waiting << tr("%1 changes not sent yet to %2").arg(count).arg(folder.account);
    }
    return waiting;
}

CalDavSource::State CalDavSource::readState(const QString &folder)
{
    const QJsonObject json = readJson(folder + u'/' + kStateFile);
    State state;
    state.url = QUrl(json[u"url"].toString());
    state.writable = json[u"writable"].toBool(true);
    state.syncToken = json[u"syncToken"].toString();
    const QJsonObject files = json[u"files"].toObject();
    for (auto it = files.begin(); it != files.end(); ++it)
        state.files.insert(it.key(), {QUrl(it->toObject()[u"href"].toString()),
                                      it->toObject()[u"etag"].toString()});
    const QJsonObject pending = json[u"pending"].toObject();
    for (auto it = pending.begin(); it != pending.end(); ++it)
        state.pending.insert(it.key(), it->toBool());
    return state;
}

void CalDavSource::writeState(const QString &folder, const State &state)
{
    QJsonObject files;
    for (auto it = state.files.cbegin(); it != state.files.cend(); ++it)
        files.insert(it.key(),
                     QJsonObject{{u"href"_s, it->href.toString()}, {u"etag"_s, it->etag}});
    QJsonObject pending;
    for (auto it = state.pending.cbegin(); it != state.pending.cend(); ++it)
        pending.insert(it.key(), it.value());
    writeJson(folder + u'/' + kStateFile, {{u"url"_s, state.url.toString()},
                                           {u"writable"_s, state.writable},
                                           {u"syncToken"_s, state.syncToken},
                                           {u"files"_s, files},
                                           {u"pending"_s, pending}});
}

CalDavSource::AccountState CalDavSource::readAccount(const QString &path)
{
    const QJsonObject json = readJson(path + u'/' + kAccountFile);
    AccountState state;
    state.home = QUrl(json[u"home"].toString());
    for (const QJsonValue &address : json[u"addresses"].toArray())
        state.addresses << address.toString();
    state.synced = QDateTime::fromString(json[u"synced"].toString(), Qt::ISODate);
    state.error = json[u"error"].toString();
    return state;
}

void CalDavSource::writeAccount(const QString &path, const AccountState &state)
{
    writeJson(path + u'/' + kAccountFile,
              {{u"home"_s, state.home.toString()},
               {u"addresses"_s, QJsonArray::fromStringList(state.addresses)},
               {u"synced"_s, state.synced.toString(Qt::ISODate)},
               {u"error"_s, state.error}});
}

} // namespace callie
