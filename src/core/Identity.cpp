#include "callie/Identity.h"

#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace callie::identity {

Names namesFor(bool devel)
{
    const QString base = QStringLiteral(CALLIE_APP_ID);
    if (devel)
        return {base + u".Devel"_s, u"callie-devel"_s, u"Callie Devel"_s};
    return {base, u"callie"_s, u"Callie"_s};
}

QString appId()
{
    return namesFor(isDevel()).appId;
}

QString dirName()
{
    return namesFor(isDevel()).dirName;
}

QString displayName()
{
    return namesFor(isDevel()).displayName;
}

bool isDevel()
{
    return CALLIE_DEVEL_BUILD;
}

bool isFlatpak()
{
    // CALLIE_FLATPAK=0 or 1 decides, as tests need wherever they run.
    const QByteArray forced = qgetenv("CALLIE_FLATPAK");
    if (!forced.isEmpty())
        return forced != "0";
    return QFileInfo::exists(QStringLiteral("/.flatpak-info"));
}

} // namespace callie::identity
