#pragma once

#include <QByteArrayView>
#include <QDate>
#include <QString>

#include <optional>

namespace callie {

/// One version's section of CHANGELOG.md.
struct ReleaseNotes
{
    QString version;
    QDate date;
    /// The section's text, Markdown, without its heading.
    QString body;

    /// The newest released section of `changelog`, written as
    /// "## 1.2.0 - 2026-10-06"; empty when there is none.
    [[nodiscard]] static std::optional<ReleaseNotes> latest(QByteArrayView changelog);

    /// The CHANGELOG.md built into Callie.
    [[nodiscard]] static QByteArray bundledChangelog();
};

} // namespace callie
