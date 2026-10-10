#include "callie/Identity.h"

namespace callie::identity {

QString appId()
{
    return QStringLiteral(CALLIE_RUN_ID);
}

QString dirName()
{
    return QStringLiteral(CALLIE_DIR_NAME);
}

QString displayName()
{
    return isDevel() ? QStringLiteral("Callie Devel") : QStringLiteral("Callie");
}

bool isDevel()
{
    return CALLIE_DEVEL_BUILD;
}

} // namespace callie::identity
