#include "callie/CalDavSource.h"

#include "callie/Identity.h"
#include "callie/Logging.h"
#include "callie/Settings.h"
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
#include <QUuid>

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

QString readText(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

void writeMeta(const QString &path, const QString &value)
{
    QSaveFile file(path);
    const QByteArray data = value.toUtf8() + '\n';
    if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size())
        file.commit();
}

/// Offline, signed out, busy or failing: worth trying again at the next sync.
bool later(const DavError &error)
{
    return error.status == 0 || error.isAuth() || error.status == 408 || error.status == 429 ||
           error.status >= 500;
}

/// Whether two addresses are the same server, however they are written.
bool sameServer(const QUrl &a, const QUrl &b)
{
    const auto plain = [](const QUrl &url) {
        QString path = url.path();
        while (path.endsWith(u'/'))
            path.chop(1);
        return u"%1://%2:%3%4"_s.arg(url.scheme().toLower(), url.host().toLower())
            .arg(url.port(url.scheme().toLower() == u"http" ? 80 : 443))
            .arg(path);
    };
    return plain(a) == plain(b);
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
    const QStringList had = m_clients.keys();
    for (const QString &id : had) {
        if (!hasAccount(id))
            stop(id);
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
    stop(accountId);
    scan();
    return true;
}

bool CalDavSource::resync(const QString &accountId, QString *error)
{
    const auto account = std::ranges::find_if(
        m_accounts, [&accountId](const Account &each) { return each.id == accountId; });
    if (account == m_accounts.end()) {
        *error = tr("There is no CalDAV account %1.").arg(accountId);
        return false;
    }
    stop(accountId);
    const QString path = accountPath(accountId);
    if (QDir(path).exists() && !QDir(path).removeRecursively()) {
        *error = tr("Callie could not delete %1.").arg(path);
        return false;
    }
    scan();
    sync(*account);
    return true;
}

bool CalDavSource::hasAccount(const QString &accountId) const
{
    return std::ranges::any_of(m_accounts,
                               [&accountId](const Account &each) { return each.id == accountId; });
}

void CalDavSource::stop(const QString &accountId)
{
    // Deleted now, so no answer to it can arrive and write for an account gone.
    delete m_clients.take(accountId);
    m_again.remove(accountId);
    if (m_running.remove(accountId)) {
        Q_EMIT statusChanged();
        if (m_running.isEmpty())
            Q_EMIT synced();
    }
}

void CalDavSource::refresh()
{
    scan();
    for (const Account &account : std::as_const(m_accounts))
        sync(account);
    VdirSource::refresh();
}

bool CalDavSource::setLook(const QString &calendarId, const QString &name, const QColor &color,
                           QString *error)
{
    const QList<Folder> all = folders();
    const auto found = std::ranges::find(all, calendarId, &Folder::id);
    State state = found != all.cend() ? readState(found->path) : State();
    if (found == all.cend() || state.gone) {
        if (error)
            *error = tr("No CalDAV calendar has the id %1.").arg(calendarId);
        return false;
    }
    const QString path = found->path;
    if (!name.trimmed().isEmpty()) {
        writeMeta(path + u"/displayname"_s, name.trimmed());
        state.lookName = true;
    }
    if (color.isValid()) {
        writeMeta(path + u"/color"_s, color.name());
        state.lookColor = true;
    }
    state.look = ++state.seq;
    writeState(path, state);
    m_soon.start();
    Q_EMIT changed();
    Q_EMIT statusChanged();
    return true;
}

void CalDavSource::followLooks(Settings *settings)
{
    m_looks = settings->calendarLooks();
    const auto take = [this, settings] {
        const QVariantMap looks = settings->calendarLooks();
        const QVariantMap before = std::exchange(m_looks, looks);
        const QList<Folder> all = folders();
        for (auto it = looks.cbegin(); it != looks.cend(); ++it) {
            if (std::ranges::find(all, it.key(), &Folder::id) == all.cend())
                continue;
            // Only the part just given; one given before stays as it was.
            const QVariantMap look = it->toMap();
            const QVariantMap old = before.value(it.key()).toMap();
            const auto given = [&look, &old](const QString &part) {
                const QString value = look.value(part).toString();
                return value == old.value(part).toString() ? QString() : value;
            };
            const QString name = given(u"name"_s);
            const QColor color = QColor::fromString(given(u"color"_s));
            if (name.isEmpty() && !color.isValid())
                continue;
            QString error;
            if (!setLook(it.key(), name, color, &error)) {
                qCWarning(lcSync) << error;
                continue;
            }
            if (!name.isEmpty())
                settings->setCalendarName(it.key(), {});
            if (color.isValid())
                settings->setCalendarColor(it.key(), {});
        }
    };
    connect(settings, &Settings::calendarLooksChanged, this, take);
}

void CalDavSource::fileChanged(const Folder &folder, const QString &fileName, bool removed)
{
    State state = readState(folder.path);
    state.pending.insert(fileName, {removed, ++state.seq});
    writeState(folder.path, state);
    m_soon.start();
    Q_EMIT statusChanged();
}

void CalDavSource::signIn(const Account &account,
                          std::function<void(CalDavClient *client, const QString &error)> done)
{
    if (!m_network || !m_tokens) {
        done(nullptr, tr("Callie is not online."));
        return;
    }
    const QPointer<CalDavSource> self(this);
    m_tokens->read({u"caldav"_s, account.id}, [self, account, done](const QString &password,
                                                                    const QString &keyring) {
        if (!self)
            return;
        // Removed while the keyring was asked: no client comes back for it.
        if (!self->hasAccount(account.id)) {
            done(nullptr, tr("That account is not connected."));
            return;
        }
        if (!keyring.isEmpty() || password.isEmpty()) {
            done(nullptr, keyring.isEmpty()
                              ? tr("No password is stored for this account; sign in again.")
                              : keyring);
            return;
        }
        CalDavClient *&client = self->m_clients[account.id];
        if (!client)
            client = new CalDavClient(self->m_network, self);
        client->setCredentials(account.user, password);
        done(client, {});
    });
}

void CalDavSource::createCalendar(const QString &accountId, const QString &name,
                                  const QColor &color,
                                  std::function<void(const QString &, const QString &)> done)
{
    const auto account = std::ranges::find(m_accounts, accountId, &Account::id);
    const QString trimmed = name.trimmed();
    if (account == m_accounts.cend() || trimmed.isEmpty()) {
        done({}, account == m_accounts.cend() ? tr("That account is not connected.")
                                              : tr("Give the calendar a name."));
        return;
    }
    const QString path = accountPath(accountId);
    QUrl home = readAccount(path).home;
    if (!home.isValid()) {
        done({}, tr("That account has not synced yet; try again once it has."));
        return;
    }
    if (!home.path().endsWith(u'/'))
        home.setPath(home.path() + u'/');
    // Named after the calendar, made unique, as servers want new addresses.
    const QString folderName =
        slug(trimmed) + u'-' + QUuid::createUuid().toString(QUuid::Id128).left(8);
    const QUrl url = home.resolved(QUrl(folderName + u'/'));
    const QPointer<CalDavSource> self(this);
    const Account signer = *account;
    whenIdle(accountId, [self, signer, url, trimmed, color, path, folderName, done] {
        const auto finish = [self, id = signer.id, done](const QString &made,
                                                         const QString &error) {
            if (!self)
                return;
            self->doneChanging(id);
            done(made, error);
        };
        self->signIn(signer, [self, url, trimmed, color, path, folderName, id = signer.id,
                              finish](CalDavClient *client, const QString &signInError) {
            if (!client) {
                finish({}, signInError);
                return;
            }
            client->makeCalendar(
                url, trimmed, color,
                [self, url, trimmed, color, path, folderName, id, finish](const DavError &error) {
                    if (!self)
                        return;
                    if (error) {
                        finish({}, error.message);
                        return;
                    }
                    const QString folder = path + u'/' + folderName;
                    QDir().mkpath(folder);
                    State state;
                    state.url = url;
                    writeState(folder, state);
                    writeMeta(folder + u"/displayname"_s, trimmed);
                    if (color.isValid())
                        writeMeta(folder + u"/color"_s, color.name());
                    self->scan();
                    Q_EMIT self->changed();
                    finish(u"caldav:"_s + slug(id) + u'/' + folderName, {});
                });
        });
    });
}

void CalDavSource::removeCalendar(const QString &calendarId,
                                  std::function<void(const QString &)> done)
{
    const QList<Folder> all = folders();
    const auto found = std::ranges::find(all, calendarId, &Folder::id);
    const auto account = found == all.cend()
                             ? m_accounts.cend()
                             : std::ranges::find(m_accounts, found->account, &Account::id);
    if (account == m_accounts.cend()) {
        done(tr("That calendar is not in a connected account."));
        return;
    }
    const QString folder = found->path;
    const QUrl url = readState(folder).url;
    const QPointer<CalDavSource> self(this);
    const Account signer = *account;
    whenIdle(signer.id, [self, signer, folder, url, done] {
        const auto finish = [self, id = signer.id, done](const QString &error) {
            if (!self)
                return;
            self->doneChanging(id);
            done(error);
        };
        self->signIn(signer,
                     [self, folder, url, finish](CalDavClient *client, const QString &signInError) {
                         if (!client) {
                             finish(signInError);
                             return;
                         }
                         client->removeCalendar(url, [self, folder, finish](const DavError &error) {
                             if (!self)
                                 return;
                             if (!error) {
                                 QDir(folder).removeRecursively();
                                 self->scan();
                                 Q_EMIT self->changed();
                             }
                             finish(error.message);
                         });
                     });
    });
}

void CalDavSource::sync(const Account &account)
{
    if (m_running.contains(account.id) || m_changing.contains(account.id)) {
        m_again.insert(account.id);
        return;
    }
    if (!m_network || !m_tokens)
        return;
    m_running.insert(account.id);
    Q_EMIT statusChanged();
    const QPointer<CalDavSource> self(this);
    signIn(account, [self, account](CalDavClient *client, const QString &signInError) {
        // Stopped meanwhile, as when the account was removed.
        if (!self || !self->m_running.contains(account.id))
            return;
        if (!client) {
            self->finish(account, signInError);
            return;
        }
        const QString path = self->accountPath(account.id);
        AccountState state = readAccount(path);
        // Signed in again to another server: the copies are of the old one.
        if (!state.server.isEmpty() && !sameServer(QUrl(state.server), account.server)) {
            QDir(path).removeRecursively();
            self->scan();
            state = {};
        }
        const auto listed = [self, account](const QList<DavCalendar> &calendars,
                                            const DavError &error, QStringList problems) {
            if (!self)
                return;
            if (error) {
                self->finish(account, error.message);
                return;
            }
            problems += self->updateCalendars(account, calendars);
            QList<Folder> mine;
            for (const Folder &folder : self->folders()) {
                if (folder.account == account.id && !readState(folder.path).gone)
                    mine.append(folder);
            }
            self->syncFolders(account, mine, problems);
        };
        // New names and colors go first, so the listing has them.
        if (state.home.isValid()) {
            QList<Folder> looks;
            for (const Folder &folder : self->folders()) {
                const State looked = readState(folder.path);
                if (folder.account == account.id && looked.look != 0 && !looked.gone)
                    looks.append(folder);
            }
            self->sendLooks(client, looks, {},
                            [client, home = state.home, listed](const QStringList &problems) {
                                client->calendars(
                                    home, [listed, problems](const QList<DavCalendar> &calendars,
                                                             const DavError &error) {
                                        listed(calendars, error, problems);
                                    });
                            });
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
            state.server = account.server.toString();
            state.home = found.home;
            state.addresses = found.addresses;
            QDir().mkpath(path);
            writeAccount(path, state);
            listed(found.calendars, {}, {});
        });
    });
}

QStringList CalDavSource::updateCalendars(const Account &account,
                                          const QList<DavCalendar> &calendars)
{
    // Compared by path, since servers write the same address more than one way.
    const auto same = [](const QUrl &url) {
        QString path = url.path(QUrl::FullyDecoded);
        while (path.endsWith(u'/'))
            path.chop(1);
        return url.host() + path;
    };
    const QString path = accountPath(account.id);
    const QDir dir(path);
    QHash<QString, QString> byUrl;
    for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        byUrl.insert(same(readState(dir.filePath(name)).url), dir.filePath(name));
    QSet<QString> seen;
    for (const DavCalendar &calendar : calendars) {
        QString folder = byUrl.value(same(calendar.url));
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
        state.gone = false;
        writeState(folder, state);
        // A new name or color not sent yet stays until it is.
        if (!state.lookName)
            writeMeta(folder + u"/displayname"_s, calendar.displayName.isEmpty()
                                                      ? QDir(folder).dirName()
                                                      : calendar.displayName);
        if (!state.lookColor && calendar.color.isValid())
            writeMeta(folder + u"/color"_s, calendar.color.name());
    }
    // Calendars gone from the server go from here too, unless changes made
    // here wait in them: those stay, read-only and unsynced, for the user.
    QStringList problems;
    for (const QString &folder : std::as_const(byUrl)) {
        if (seen.contains(folder))
            continue;
        State state = readState(folder);
        if (state.pending.isEmpty()) {
            QDir(folder).removeRecursively();
            continue;
        }
        state.gone = true;
        state.writable = false;
        writeState(folder, state);
        problems << tr("%1 is gone from the server, with changes not sent; they are kept in %2.")
                        .arg(readText(folder + u"/displayname"_s), folder);
    }
    scan();
    return problems;
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

void CalDavSource::sendLooks(CalDavClient *client, QList<Folder> folders, QStringList errors,
                             std::function<void(const QStringList &errors)> done)
{
    if (folders.isEmpty()) {
        done(errors);
        return;
    }
    const Folder folder = folders.takeFirst();
    const State state = readState(folder.path);
    // Only what changed here, so a rename made elsewhere is not undone.
    const QString name = state.lookName ? readText(folder.path + u"/displayname"_s) : QString();
    const QColor color =
        state.lookColor ? QColor::fromString(readText(folder.path + u"/color"_s)) : QColor();
    const QPointer<CalDavSource> self(this);
    client->setLook(state.url, name, color,
                    [self, client, folder, folders, errors, done,
                     sent = state.look](const DavError &error) mutable {
                        if (!self)
                            return;
                        // Turned down, the server's own name and color come
                        // back with the listing; a change made meanwhile waits.
                        State now = readState(folder.path);
                        if (error && later(error)) {
                            errors << error.message;
                        } else if (now.look == sent) {
                            now.look = 0;
                            now.lookName = now.lookColor = false;
                            writeState(folder.path, now);
                            if (error) {
                                errors << error.message;
                                Q_EMIT self->errorOccurred(error.message);
                            }
                        }
                        self->sendLooks(client, folders, errors, done);
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
    const Waiting waiting = state.pending.value(fileName);
    const Remote remote = state.files.value(fileName);
    const QPointer<CalDavSource> self(this);
    // Sent, or settled another way: on to the next. A change made meanwhile
    // stays waiting, to go over the version just sent.
    const auto next = [self, client, folder, fileName, waiting, done](const Remote &now,
                                                                      bool gone) {
        if (!self)
            return;
        State state = readState(folder.path);
        if (state.pending.value(fileName).seq == waiting.seq)
            state.pending.remove(fileName);
        if (gone)
            state.files.remove(fileName);
        else
            state.files.insert(fileName, now);
        writeState(folder.path, state);
        self->push(client, folder, done);
    };
    // Turned down by the server: its copy stands, and the user hears why,
    // unless a newer change has been made since, which goes next.
    const auto refused = [self, client, folder, fileName, waiting, remote,
                          done](const DavError &error) {
        if (!self)
            return;
        State state = readState(folder.path);
        if (state.pending.value(fileName).seq != waiting.seq) {
            self->push(client, folder, done);
            return;
        }
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
    if (waiting.removed) {
        if (!remote.href.isValid()) {
            next({}, true);
            return;
        }
        client->remove(remote.href, remote.etag, [next, refused, done](const DavError &error) {
            if (!error)
                next({}, true);
            else if (later(error))
                done(error.message);
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
                    else if (later(error))
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
            self->apply(client, folder, changed, removed, {}, false, done);
        });
    };
    client->changes(state.url, state.syncToken,
                    [self, client, folder, done, byVersions, token = state.syncToken](
                        const CalDavClient::Changes &changes, const DavError &error) {
                        if (!self)
                            return;
                        if (!error) {
                            // From no token, the answer is everything there is.
                            self->apply(client, folder, changes.changed, changes.removed,
                                        changes.token, token.isEmpty(), done);
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
                         QList<QUrl> removed, const QString &token, bool whole, Done done)
{
    const State state = readState(folder.path);
    QHash<QString, QString> fileOf;
    for (auto it = state.files.cbegin(); it != state.files.cend(); ++it)
        fileOf.insert(it->href.toString(), it.key());
    if (whole) {
        QSet<QString> there;
        for (const DavItem &item : changed)
            there.insert(item.href.toString());
        for (const Remote &remote : state.files) {
            if (!there.contains(remote.href.toString()))
                removed << remote.href;
        }
    }
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
    next(account.id);
    if (m_running.isEmpty())
        Q_EMIT synced();
}

void CalDavSource::whenIdle(const QString &accountId, std::function<void()> work)
{
    if (m_running.contains(accountId) || m_changing.contains(accountId)) {
        m_waiting[accountId].append(std::move(work));
        return;
    }
    m_changing.insert(accountId);
    work();
}

void CalDavSource::doneChanging(const QString &accountId)
{
    m_changing.remove(accountId);
    next(accountId);
}

void CalDavSource::next(const QString &accountId)
{
    // Calendar changes go before a sync asked for meanwhile, which then sees them.
    if (const auto waiting = m_waiting.find(accountId); waiting != m_waiting.end()) {
        const std::function<void()> work = waiting->takeFirst();
        if (waiting->isEmpty())
            m_waiting.erase(waiting);
        m_changing.insert(accountId);
        work();
        return;
    }
    if (m_again.remove(accountId)) {
        const auto again = std::ranges::find(m_accounts, accountId, &Account::id);
        if (again != m_accounts.end())
            sync(*again);
    }
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
        const State state = readState(folder.path);
        const qsizetype count = state.pending.size() + (state.look != 0 ? 1 : 0);
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
    state.gone = json[u"gone"].toBool();
    state.syncToken = json[u"syncToken"].toString();
    state.look = json[u"look"].toInteger();
    state.lookName = json[u"lookName"].toBool();
    state.lookColor = json[u"lookColor"].toBool();
    const QJsonObject files = json[u"files"].toObject();
    for (auto it = files.begin(); it != files.end(); ++it)
        state.files.insert(it.key(), {QUrl(it->toObject()[u"href"].toString()),
                                      it->toObject()[u"etag"].toString()});
    const QJsonObject pending = json[u"pending"].toObject();
    for (auto it = pending.begin(); it != pending.end(); ++it)
        state.pending.insert(
            it.key(), {it->toObject()[u"removed"].toBool(), it->toObject()[u"seq"].toInteger()});
    state.seq = json[u"seq"].toInteger();
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
        pending.insert(it.key(), QJsonObject{{u"removed"_s, it->removed}, {u"seq"_s, it->seq}});
    writeJson(folder + u'/' + kStateFile, {{u"url"_s, state.url.toString()},
                                           {u"writable"_s, state.writable},
                                           {u"gone"_s, state.gone},
                                           {u"syncToken"_s, state.syncToken},
                                           {u"files"_s, files},
                                           {u"pending"_s, pending},
                                           {u"look"_s, state.look},
                                           {u"lookName"_s, state.lookName},
                                           {u"lookColor"_s, state.lookColor},
                                           {u"seq"_s, state.seq}});
}

CalDavSource::AccountState CalDavSource::readAccount(const QString &path)
{
    const QJsonObject json = readJson(path + u'/' + kAccountFile);
    AccountState state;
    state.server = json[u"server"].toString();
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
              {{u"server"_s, state.server},
               {u"home"_s, state.home.toString()},
               {u"addresses"_s, QJsonArray::fromStringList(state.addresses)},
               {u"synced"_s, state.synced.toString(Qt::ISODate)},
               {u"error"_s, state.error}});
}

} // namespace callie
