#include "SupportActions.h"

#include "callie/Diagnostics.h"
#include "callie/GoogleCache.h"
#include "callie/LogFile.h"
#include "callie/QueuedSource.h"
#include "callie/Settings.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPointer>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace callie {

void SupportActions::collect(std::function<void(const QString &)> done)
{
    const QString extra = QStringLiteral("Qt platform: %1").arg(QGuiApplication::platformName());
    diagnostics::collect(m_tokens, extra,
                         [self = QPointer(this), done = std::move(done)](const QString &report) {
                             if (self)
                                 done(report);
                         });
}

void SupportActions::copyDebugInfo()
{
    collect([this](const QString &report) {
        QGuiApplication::clipboard()->setText(report);
        Q_EMIT copied();
    });
}

void SupportActions::openLogs()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(logfile::defaultDirectory()));
}

void SupportActions::openFolder(const QString &which)
{
    const QString file = which == u"config"_s ? Settings::defaultPath()
                         : which == u"data"_s ? QueuedSource::defaultPath()
                                              : GoogleCache::defaultPath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(file).absolutePath()));
}

void SupportActions::copyText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
    Q_EMIT copied();
}

void SupportActions::reportBug()
{
    collect(
        [](const QString &report) { QDesktopServices::openUrl(diagnostics::issueUrl(report)); });
}

} // namespace callie
