#include "callie/ThemeLibrary.h"

#include "callie/ThemeLoader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

QString sourceFile(const QString &idOrPath)
{
    return ThemeLoader::isPath(idOrPath) ? idOrPath : u":/callie/themes/"_s + idOrPath + u".toml"_s;
}

QByteArray read(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QObject::tr("could not read %1: %2").arg(path, file.errorString());
        return {};
    }
    return file.readAll();
}

bool write(const QString &path, const QByteArray &data, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        *error = QObject::tr("could not write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

/// Sets `key = value` inside `[section]`, adding the section or the key when
/// missing, and leaves every other line, comments included, alone.
QString setKey(const QString &toml, const QString &section, const QString &key,
               const QString &value)
{
    QStringList lines = toml.split(u'\n');
    const QString line = u"%1 = %2"_s.arg(key, value);
    const QRegularExpression keyLine(u"^\\s*"_s + QRegularExpression::escape(key) + u"\\s*="_s);
    const QRegularExpression header(u"^\\s*\\[([^\\]]+)\\]"_s);
    qsizetype start = -1;
    qsizetype end = lines.size();
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const auto match = header.match(lines.at(i));
        if (!match.hasMatch())
            continue;
        if (start >= 0) {
            end = i;
            break;
        }
        if (match.captured(1).trimmed() == section)
            start = i;
    }
    if (start < 0) {
        if (!lines.isEmpty() && !lines.last().isEmpty())
            lines.append(QString());
        lines << u"[%1]"_s.arg(section) << line << QString();
        return lines.join(u'\n');
    }
    for (qsizetype i = start + 1; i < end; ++i) {
        if (keyLine.match(lines.at(i)).hasMatch()) {
            lines[i] = line;
            return lines.join(u'\n');
        }
    }
    // After the section's last key, not after the blank lines that follow it.
    qsizetype at = end;
    while (at > start + 1 && lines.at(at - 1).trimmed().isEmpty())
        --at;
    lines.insert(at, line);
    return lines.join(u'\n');
}

QString quoted(const QString &text)
{
    QString escaped = text;
    escaped.replace(u'\\', u"\\\\"_s).replace(u'"', u"\\\""_s);
    return u'"' + escaped + u'"';
}

} // namespace

ThemeLibrary::ThemeLibrary(QString folder) : m_folder(std::move(folder)) {}

QString ThemeLibrary::defaultFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           u"/callie/themes"_s;
}

QStringList ThemeLibrary::themes() const
{
    QStringList paths;
    for (const QFileInfo &file :
         QDir(m_folder).entryInfoList({u"*.toml"_s}, QDir::Files, QDir::Name | QDir::IgnoreCase))
        paths << file.absoluteFilePath();
    return paths;
}

QStringList ThemeLibrary::colorKeys()
{
    return {u"background"_s,  u"surface"_s,    u"surface-alt"_s, u"hairline"_s, u"border"_s,
            u"text"_s,        u"text-muted"_s, u"text-faint"_s,  u"accent"_s,   u"accent-text"_s,
            u"accent-edge"_s, u"edge"_s,       u"danger"_s};
}

QString ThemeLibrary::displayName(const QString &idOrPath)
{
    const ThemeLoadResult result = ThemeLoader::load(idOrPath);
    if (result.ok() && !result.theme.name.isEmpty() &&
        (result.theme.name != ThemeLoader::defaultTheme().name || !ThemeLoader::isPath(idOrPath)))
        return result.theme.name;
    return QFileInfo(idOrPath).completeBaseName();
}

QString ThemeLibrary::copy(const QString &idOrPath, const QString &name)
{
    QString problem;
    const QByteArray data = read(sourceFile(idOrPath), &problem);
    if (!problem.isEmpty()) {
        fail(problem);
        return {};
    }
    if (!QDir().mkpath(m_folder)) {
        fail(QObject::tr("could not create %1").arg(m_folder));
        return {};
    }
    const QString path = freshPath(name);
    const QString renamed = setKey(QString::fromUtf8(data), u"theme"_s, u"name"_s, quoted(name));
    if (!write(path, renamed.toUtf8(), &problem)) {
        fail(problem);
        return {};
    }
    return path;
}

bool ThemeLibrary::setColor(const QString &path, const QString &key, const QColor &color)
{
    if (!owns(path))
        return fail(QObject::tr("%1 is not one of your themes").arg(path));
    if (!colorKeys().contains(key))
        return fail(QObject::tr("themes have no color named %1").arg(key));
    if (!color.isValid())
        return fail(QObject::tr("not a color"));
    QString problem;
    const QByteArray data = read(path, &problem);
    if (!problem.isEmpty())
        return fail(problem);
    const QString updated = setKey(QString::fromUtf8(data), u"colors"_s, key, quoted(color.name()));
    // Check before writing, so a bad key never reaches the file being watched.
    const ThemeLoadResult result =
        ThemeLoader::parse(updated.toUtf8(), ThemeLoader::defaultTheme());
    if (!result.ok())
        return fail(result.errors.join(u"; "_s));
    return write(path, updated.toUtf8(), &problem) || fail(problem);
}

QString ThemeLibrary::importTheme(const QString &source)
{
    const ThemeLoadResult result = ThemeLoader::loadFile(source, ThemeLoader::defaultTheme());
    if (!result.ok()) {
        fail(result.errors.join(u"; "_s));
        return {};
    }
    QString problem;
    const QByteArray data = read(source, &problem);
    if (!problem.isEmpty()) {
        fail(problem);
        return {};
    }
    if (!QDir().mkpath(m_folder)) {
        fail(QObject::tr("could not create %1").arg(m_folder));
        return {};
    }
    const QString name =
        result.theme.name.isEmpty() ? QFileInfo(source).completeBaseName() : result.theme.name;
    const QString path = freshPath(name);
    if (!write(path, data, &problem)) {
        fail(problem);
        return {};
    }
    return path;
}

bool ThemeLibrary::exportTheme(const QString &idOrPath, const QString &destination)
{
    QString problem;
    const QByteArray data = read(sourceFile(idOrPath), &problem);
    if (!problem.isEmpty())
        return fail(problem);
    return write(destination, data, &problem) || fail(problem);
}

bool ThemeLibrary::remove(const QString &path)
{
    if (!owns(path))
        return fail(QObject::tr("%1 is not one of your themes").arg(path));
    return QFile::remove(path) || fail(QObject::tr("could not delete %1").arg(path));
}

bool ThemeLibrary::owns(const QString &path) const
{
    const QFileInfo file(path);
    return file.suffix() == u"toml" &&
           file.absoluteDir().canonicalPath() == QDir(m_folder).canonicalPath();
}

QString ThemeLibrary::freshPath(const QString &name) const
{
    QString base = name.toLower();
    base.replace(QRegularExpression(u"[^a-z0-9]+"_s), u"-"_s);
    base = base.remove(QRegularExpression(u"^-+|-+$"_s));
    if (base.isEmpty())
        base = u"theme"_s;
    QString path = m_folder + u'/' + base + u".toml"_s;
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = m_folder + u'/' + base + u'-' + QString::number(n) + u".toml"_s;
    return path;
}

bool ThemeLibrary::fail(const QString &message)
{
    m_error = message;
    return false;
}

} // namespace callie
