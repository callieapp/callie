#include "callie/LocalCalendars.h"

#include "callie/Identity.h"

#include <QDir>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

bool writeMeta(const QString &path, const QString &value)
{
    QSaveFile file(path);
    const QByteArray data = value.toUtf8() + '\n';
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

} // namespace

LocalCalendars::LocalCalendars(QString root, QObject *parent)
    : VdirSource(parent), m_root(std::move(root))
{
    connect(&m_rootWatcher, &QFileSystemWatcher::directoryChanged, this, &LocalCalendars::scan);
    scan();
}

QString LocalCalendars::defaultRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u'/' +
           identity::dirName() + u"/calendars"_s;
}

QString LocalCalendars::idFor(const QString &folderName)
{
    return u"local:"_s + folderName;
}

void LocalCalendars::scan()
{
    if (!m_rootWatcher.directories().isEmpty())
        m_rootWatcher.removePaths(m_rootWatcher.directories());
    if (QDir(m_root).exists())
        m_rootWatcher.addPath(m_root);
    QList<Folder> folders;
    const QDir root(m_root);
    for (const QString &name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        folders.append({idFor(name), root.filePath(name), true, QString(kAccount)});
    const auto same = [](const Folder &a, const Folder &b) { return a.id == b.id; };
    const QList<Folder> before = this->folders();
    if (!std::ranges::equal(folders, before, same))
        setFolders(folders);
}

void LocalCalendars::refresh()
{
    scan();
    VdirSource::refresh();
}

QString LocalCalendars::create(const QString &name, const QColor &color, QString *error)
{
    const QString trimmed = name.simplified();
    if (trimmed.isEmpty()) {
        *error = tr("A calendar needs a name.");
        return {};
    }
    // A folder name a person would recognise, kept apart from any taken.
    QString base = trimmed.toLower();
    base.replace(QRegularExpression(u"[^\\w-]+"_s), u"-"_s);
    base.remove(QRegularExpression(u"^[-.]+|-+$"_s));
    if (base.isEmpty())
        base = u"calendar"_s;
    const QDir root(m_root);
    QString folder = base;
    for (int n = 2; root.exists(folder); ++n)
        folder = base + u'-' + QString::number(n);
    // Without a color of its own, the first one no other calendar has.
    QColor shown = color;
    if (!shown.isValid()) {
        const QList<CalendarInfo> taken = calendars();
        const QList<QColor> colors = palette();
        const auto unused = std::ranges::find_if(colors, [&taken](const QColor &candidate) {
            return std::ranges::none_of(
                taken, [&candidate](const CalendarInfo &c) { return c.color == candidate; });
        });
        shown = unused == colors.end() ? colors.at(int(taken.size() % colors.size())) : *unused;
    }
    const QString path = root.filePath(folder);
    if (!QDir().mkpath(path) || !writeMeta(path + u"/displayname"_s, trimmed) ||
        !writeMeta(path + u"/color"_s, shown.name())) {
        *error = tr("Callie could not create %1.").arg(path);
        return {};
    }
    scan();
    return idFor(folder);
}

bool LocalCalendars::remove(const QString &id, QString *error)
{
    const QList<Folder> all = folders();
    const auto found =
        std::ranges::find_if(all, [&id](const Folder &folder) { return folder.id == id; });
    if (found == all.end()) {
        *error = tr("That calendar is not on this computer.");
        return false;
    }
    if (!QFile::moveToTrash(found->path)) {
        *error = tr("Callie could not move %1 to the trash.").arg(found->path);
        return false;
    }
    scan();
    return true;
}

} // namespace callie
