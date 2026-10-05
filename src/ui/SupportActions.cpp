#include "SupportActions.h"

#include "callie/Diagnostics.h"
#include "callie/LogFile.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QPointer>
#include <QUrl>

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

void SupportActions::reportBug()
{
    collect(
        [](const QString &report) { QDesktopServices::openUrl(diagnostics::issueUrl(report)); });
}

} // namespace callie
