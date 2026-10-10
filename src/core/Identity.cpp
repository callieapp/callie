#include "callie/Identity.h"

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

} // namespace callie::identity
