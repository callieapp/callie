#include "callie/VdirSource.h"

#include <QDir>
#include <QFileInfo>
#include <QPromise>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

QString readMeta(const QString &folder, const QString &name)
{
    QFile file(folder + u'/' + name);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll()).trimmed();
}

/// A file name for an event: its UID when that makes a safe one, as
/// vdirsyncer does, else a fresh one.
QString fileNameFor(const QString &uid)
{
    static const QRegularExpression safe(u"^[A-Za-z0-9._@-]{1,200}$"_s);
    return (safe.match(uid).hasMatch() && !uid.startsWith(u'.')
                ? uid
                : QUuid::createUuid().toString(QUuid::WithoutBraces)) +
           u".ics"_s;
}

} // namespace

VdirSource::VdirSource(QObject *parent) : CalendarSource(parent)
{
    m_readers.setMaxThreadCount(1);
    m_settle.setSingleShot(true);
    m_settle.setInterval(300);
    connect(&m_settle, &QTimer::timeout, this, &CalendarSource::changed);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_settle,
            qOverload<>(&QTimer::start));
}

VdirSource::~VdirSource()
{
    m_readers.waitForDone();
}

void VdirSource::setFolders(const QList<Folder> &folders)
{
    m_folders = folders;
    {
        const QMutexLocker locker(&m_lock);
        m_parsed.removeIf([this](const auto &entry) {
            return std::ranges::none_of(m_folders, [&entry](const Folder &folder) {
                return entry.key().startsWith(folder.path + u'/');
            });
        });
    }
    watch();
    Q_EMIT changed();
}

void VdirSource::watch()
{
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    for (const Folder &folder : std::as_const(m_folders)) {
        if (QFileInfo(folder.path).isDir())
            m_watcher.addPath(folder.path);
    }
}

QList<QColor> VdirSource::palette()
{
    return {QColor(0x039be5), QColor(0x33b679), QColor(0x8e24aa), QColor(0xf4511e),
            QColor(0xe67c73), QColor(0xf6bf26), QColor(0x3f51b5), QColor(0x0b8043)};
}

QColor VdirSource::defaultColor(const QString &id)
{
    const QList<QColor> colors = palette();
    return colors.at(int(qHash(id) % colors.size()));
}

QList<CalendarInfo> VdirSource::calendars() const
{
    return calendarsOf(m_folders);
}

QList<CalendarInfo> VdirSource::calendarsOf(const QList<Folder> &folders)
{
    QList<CalendarInfo> calendars;
    for (const Folder &folder : folders) {
        const QString name = readMeta(folder.path, u"displayname"_s);
        const QColor color = QColor::fromString(readMeta(folder.path, u"color"_s).left(7));
        CalendarInfo info;
        info.id = folder.id;
        info.displayName = name.isEmpty() ? QDir(folder.path).dirName() : name;
        info.color = color.isValid() ? color : defaultColor(folder.id);
        info.writable = folder.writable;
        info.account = folder.account;
        calendars.append(info);
    }
    return calendars;
}

VdirSource::Contents VdirSource::read(const Folder &folder) const
{
    Contents contents;
    QHash<QString, ics::Group> byUid;
    QStringList order;
    const QFileInfoList files =
        QDir(folder.path).entryInfoList({u"*.ics"_s}, QDir::Files, QDir::Name);
    QSet<QString> present;
    for (const QFileInfo &file : files) {
        const QString path = file.absoluteFilePath();
        present.insert(path);
        ics::Group events;
        bool known = false;
        {
            const QMutexLocker locker(&m_lock);
            const auto cached = m_parsed.constFind(path);
            known = cached != m_parsed.constEnd() && cached->modified == file.lastModified() &&
                    cached->size == file.size();
            if (known)
                events = cached->events;
        }
        if (!known) {
            QFile in(path);
            if (in.open(QIODevice::ReadOnly))
                events = ics::parse(in.readAll());
            const QMutexLocker locker(&m_lock);
            m_parsed.insert(path, {file.lastModified(), file.size(), events});
        }
        for (const ics::Group &group : ics::byUid(events)) {
            const QString uid = group.first()->uid();
            if (!byUid.contains(uid)) {
                order.append(uid);
                contents.files.insert(uid, file.fileName());
            }
            byUid[uid] += group;
        }
    }
    {
        const QMutexLocker locker(&m_lock);
        m_parsed.removeIf([&folder, &present](const auto &entry) {
            return entry.key().startsWith(folder.path + u'/') && !present.contains(entry.key());
        });
    }
    for (const QString &uid : std::as_const(order))
        contents.groups.append(byUid.value(uid));
    return contents;
}

SourceSnapshot VdirSource::snapshot(const QList<Folder> &folders, const QDateTime &from,
                                    const QDateTime &to, const QTimeZone &tz) const
{
    SourceSnapshot snapshot;
    snapshot.calendars = calendarsOf(folders);
    for (qsizetype i = 0; i < folders.size(); ++i) {
        const Folder &folder = folders.at(i);
        const ics::Calendar calendar{folder.id, snapshot.calendars.at(i).color, folder.writable,
                                     folder.self};
        for (const ics::Group &group : read(folder).groups)
            snapshot.events += ics::expand(group, calendar, from, to, tz);
    }
    return snapshot;
}

QList<Event> VdirSource::eventsBetween(const QDateTime &from, const QDateTime &to,
                                       const QTimeZone &tz) const
{
    return snapshot(m_folders, from, to, tz).events;
}

QFuture<SourceSnapshot> VdirSource::load(const QDateTime &from, const QDateTime &to,
                                         const QTimeZone &tz) const
{
    auto promise = std::make_shared<QPromise<SourceSnapshot>>();
    QFuture<SourceSnapshot> future = promise->future();
    promise->start();
    m_readers.start([this, folders = m_folders, from, to, tz, promise] {
        promise->addResult(snapshot(folders, from, to, tz));
        promise->finish();
    });
    return future;
}

void VdirSource::refresh()
{
    Q_EMIT changed();
}

const VdirSource::Folder *VdirSource::folder(const QString &id) const
{
    const auto found =
        std::ranges::find_if(m_folders, [&id](const Folder &folder) { return folder.id == id; });
    return found == m_folders.end() ? nullptr : &*found;
}

bool VdirSource::find(const Event &event, const Created &done, const Folder **where,
                      ics::Group *group, QString *fileName) const
{
    *where = folder(event.calendarId);
    if (!*where) {
        done(tr("That calendar is not on this computer any more."));
        return false;
    }
    if (!(*where)->writable) {
        done(tr("This calendar cannot be changed."));
        return false;
    }
    const Contents contents = read(**where);
    *fileName = contents.files.value(event.uid);
    for (const ics::Group &candidate : contents.groups) {
        if (candidate.first()->uid() == event.uid) {
            *group = candidate;
            return true;
        }
    }
    done(tr("That event is not in this calendar any more."));
    return false;
}

bool VdirSource::save(const Folder &folder, const QString &fileName, const QString &uid,
                      const ics::Group &group, QString *error)
{
    const QString path = folder.path + u'/' + fileName;
    // Only `uid` changes; anything else the file holds stays.
    QByteArray data;
    if (QFile existing(path); existing.open(QIODevice::ReadOnly))
        data = ics::replaced(existing.readAll(), uid, group);
    else if (!group.isEmpty())
        data = ics::write(group);
    if (data.isEmpty()) {
        if (QFile::exists(path) && !QFile::remove(path)) {
            *error = tr("Callie could not delete %1.").arg(path);
            return false;
        }
    } else {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
            *error = tr("Callie could not write %1: %2").arg(path, file.errorString());
            return false;
        }
    }
    // A rewrite can keep the size and land in the same millisecond, which
    // the cache would take for no change.
    {
        const QMutexLocker locker(&m_lock);
        m_parsed.remove(path);
    }
    fileChanged(folder, fileName, data.isEmpty());
    return true;
}

bool VdirSource::mirror(const Folder &folder, const ics::Group &events, QString *error)
{
    // Gone, as when another process stopped it: not made again.
    if (!QDir(folder.path).exists()) {
        *error = tr("%1 is gone.").arg(folder.path);
        return false;
    }
    const QHash<QString, QString> before = read(folder).files;
    QSet<QString> kept;
    for (const ics::Group &group : ics::byUid(events)) {
        const QString uid = group.first()->uid();
        kept.insert(uid);
        if (!save(folder, before.value(uid, fileNameFor(uid)), uid, group, error))
            return false;
    }
    for (auto file = before.cbegin(); file != before.cend(); ++file) {
        if (!kept.contains(file.key()) && !save(folder, file.value(), file.key(), {}, error))
            return false;
    }
    return true;
}

bool VdirSource::writeFile(const Folder &folder, const QString &fileName, const QByteArray &data,
                           QString *error)
{
    const QString path = folder.path + u'/' + fileName;
    QSaveFile file(path);
    if (!QDir().mkpath(folder.path) || !file.open(QIODevice::WriteOnly) ||
        file.write(data) != data.size() || !file.commit()) {
        *error = tr("Callie could not write %1: %2").arg(path, file.errorString());
        return false;
    }
    const QMutexLocker locker(&m_lock);
    m_parsed.remove(path);
    return true;
}

bool VdirSource::removeFile(const Folder &folder, const QString &fileName, QString *error)
{
    const QString path = folder.path + u'/' + fileName;
    if (QFile::exists(path) && !QFile::remove(path)) {
        *error = tr("Callie could not delete %1.").arg(path);
        return false;
    }
    const QMutexLocker locker(&m_lock);
    m_parsed.remove(path);
    return true;
}

void VdirSource::fileChanged(const Folder &folder, const QString &fileName, bool removed)
{
    Q_UNUSED(folder)
    Q_UNUSED(fileName)
    Q_UNUSED(removed)
}

void VdirSource::apply(const Event &event, const Created &done, const Change &change)
{
    const Folder *where = nullptr;
    ics::Group group;
    QString fileName;
    if (!find(event, done, &where, &group, &fileName))
        return;
    QString error;
    const std::optional<ics::Changed> changed = change(group, &error);
    if (!changed) {
        done(error);
        return;
    }
    const Folder target = *where;
    // A series split off is written before the old one is cut short, so a
    // failure part way loses nothing.
    const QString splitUid = changed->split.isEmpty() ? QString() : changed->split.first()->uid();
    if ((!splitUid.isEmpty() &&
         !save(target, fileNameFor(splitUid), splitUid, changed->split, &error)) ||
        !save(target, fileName, event.uid, changed->group, &error)) {
        done(error);
        return;
    }
    Q_EMIT this->changed();
    done({});
}

int VdirSource::importEvents(const QString &calendarId, const ics::Group &events, QString *error)
{
    const Folder *where = folder(calendarId);
    if (!where || !where->writable) {
        *error = tr("That calendar cannot take new events.");
        return -1;
    }
    if (!QDir().mkpath(where->path)) {
        *error = tr("Callie could not create %1.").arg(where->path);
        return -1;
    }
    const Folder target = *where;
    const QHash<QString, QString> files = read(target).files;
    int written = 0;
    for (const ics::Group &group : ics::byUid(events)) {
        const QString uid = group.first()->uid();
        if (!save(target, files.value(uid, fileNameFor(uid)), uid, group, error))
            return -1;
        ++written;
    }
    if (written > 0)
        Q_EMIT changed();
    return written;
}

void VdirSource::createEvent(const EventDraft &draft, Created done)
{
    const Folder *where = folder(draft.calendarId);
    if (!where || !where->writable) {
        done(tr("That calendar cannot take new events."));
        return;
    }
    const QString uid =
        draft.id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : draft.id;
    // Sent twice, as after a retry, it is made once.
    if (read(*where).files.contains(uid)) {
        done({});
        return;
    }
    if (!QDir().mkpath(where->path)) {
        done(tr("Callie could not create %1.").arg(where->path));
        return;
    }
    QString error;
    const Folder target = *where;
    if (!save(target, fileNameFor(uid), uid, {ics::fromDraft(draft, uid)}, &error)) {
        done(error);
        return;
    }
    Q_EMIT changed();
    done({});
}

void VdirSource::respond(const Event &event, const QString &status, bool wholeSeries, Created done)
{
    const Folder *where = folder(event.calendarId);
    const QString self = where ? where->self : QString();
    apply(event, done, [&](const ics::Group &group, QString *error) {
        return ics::respond(group, event, status, wholeSeries, self, error);
    });
}

void VdirSource::deleteEvent(const Event &event, bool wholeSeries, Created done)
{
    apply(event, done, [&](const ics::Group &group, QString *error) {
        return ics::remove(group, event, wholeSeries, error);
    });
}

void VdirSource::moveEvent(const Event &event, const QDateTime &start, const QDateTime &end,
                           bool wholeSeries, Created done)
{
    apply(event, done, [&](const ics::Group &group, QString *error) {
        return ics::move(group, event, start, end, wholeSeries, error);
    });
}

void VdirSource::updateEvent(const Event &event, const EventEdit &edit, EditScope scope,
                             Created done)
{
    const QString newUid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    apply(event, done, [&](const ics::Group &group, QString *error) {
        return ics::update(group, event, edit, scope, newUid, error);
    });
}

} // namespace callie
