#include "callie/Autostart.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

// Desktop entry Exec values quote arguments that hold spaces or quotes.
QString quoted(const QString &argument)
{
    static const QString special = u" \t\"'\\$`"_s;
    if (std::none_of(argument.cbegin(), argument.cend(),
                     [](QChar c) { return special.contains(c); }))
        return argument;
    QString escaped = argument;
    for (const QString &c : {u"\\"_s, u"\""_s, u"$"_s, u"`"_s})
        escaped.replace(c, u"\\"_s + c);
    return u'"' + escaped + u'"';
}

} // namespace

Autostart::Autostart(QString appId, QString program, QString path)
    : m_appId(std::move(appId)), m_program(std::move(program)),
      m_path(path.isEmpty() ? defaultPath(m_appId) : std::move(path))
{}

QString Autostart::defaultPath(const QString &appId)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           u"/autostart/"_s + appId + u".desktop"_s;
}

bool Autostart::isEnabled() const
{
    return QFileInfo::exists(m_path);
}

bool Autostart::setEnabled(bool enabled)
{
    m_error.clear();
    if (!enabled) {
        if (QFile::exists(m_path) && !QFile::remove(m_path)) {
            m_error = QObject::tr("could not remove %1").arg(m_path);
            return false;
        }
        return true;
    }
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
        m_error = QObject::tr("could not create %1").arg(QFileInfo(m_path).absolutePath());
        return false;
    }
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_error = file.errorString();
        return false;
    }
    const QString entry = u"[Desktop Entry]\n"
                          u"Type=Application\n"
                          u"Name=Callie\n"
                          u"Comment=Keeps Callie running for event reminders\n"
                          u"Exec=%1 --%2\n"
                          u"Icon=%3\n"
                          u"Terminal=false\n"
                          u"NoDisplay=true\n"
                          u"X-GNOME-Autostart-enabled=true\n"_s.arg(
                              quoted(m_program), QString::fromLatin1(kBackgroundOption), m_appId);
    file.write(entry.toUtf8());
    if (!file.commit()) {
        m_error = file.errorString();
        return false;
    }
    return true;
}

} // namespace callie
