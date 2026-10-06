#include "callie/ReleaseNotes.h"

#include <QFile>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace callie {

std::optional<ReleaseNotes> ReleaseNotes::latest(QByteArrayView changelog)
{
    static const QRegularExpression heading(
        u"^## (\\d+\\.\\d+\\.\\d+) - (\\d{4}-\\d{2}-\\d{2})\\s*$"_s,
        QRegularExpression::MultilineOption);
    const QString text = QString::fromUtf8(changelog);
    const QRegularExpressionMatch match = heading.match(text);
    if (!match.hasMatch())
        return std::nullopt;
    const qsizetype start = match.capturedEnd();
    qsizetype end =
        text.indexOf(QRegularExpression(u"^## "_s, QRegularExpression::MultilineOption), start);
    if (end < 0)
        end = text.size();
    return ReleaseNotes{match.captured(1), QDate::fromString(match.captured(2), Qt::ISODate),
                        text.mid(start, end - start).trimmed()};
}

QByteArray ReleaseNotes::bundledChangelog()
{
    QFile file(u":/callie/CHANGELOG.md"_s);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace callie
