#include "ReleaseInfo.h"

#include "EventModelForeign.h"

#include "callie/ReleaseNotes.h"
#include "callie/Settings.h"

#include <QCoreApplication>

using namespace Qt::StringLiterals;

namespace callie {

ReleaseInfo::ReleaseInfo(QObject *parent) : QObject(parent)
{
    if (const auto notes = ReleaseNotes::latest(ReleaseNotes::bundledChangelog())) {
        m_notesVersion = notes->version;
        m_notesDate = notes->date.startOfDay();
        m_notes = notes->body;
    }
}

QString ReleaseInfo::version() const
{
    return QCoreApplication::applicationVersion();
}

QUrl ReleaseInfo::website() const
{
    return QUrl(u"https://callieapp.org/"_s);
}

QUrl ReleaseInfo::privacy() const
{
    return QUrl(u"https://callieapp.org/privacy.html"_s);
}

QUrl ReleaseInfo::terms() const
{
    return QUrl(u"https://callieapp.org/terms.html"_s);
}

QUrl ReleaseInfo::source() const
{
    return QUrl(u"https://github.com/callieapp/callie"_s);
}

QUrl ReleaseInfo::history() const
{
    return QUrl(u"https://github.com/callieapp/callie/blob/main/CHANGELOG.md"_s);
}

bool ReleaseInfo::takeUpdateNotice()
{
    Settings *settings = SettingsForeign::create(nullptr, nullptr);
    const QString last = settings->lastSeenVersion();
    settings->setLastSeenVersion(version());
    return !last.isEmpty() && last != version() && !m_notes.isEmpty();
}

} // namespace callie
