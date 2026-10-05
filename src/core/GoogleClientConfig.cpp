#include "callie/GoogleClientConfig.h"

#include "callie/GoogleBuildConfig.h"

namespace callie {

GoogleClientConfig GoogleClientConfig::builtIn()
{
    return {QString::fromLatin1(build::googleClientId),
            QString::fromLatin1(build::googleClientSecret)};
}

GoogleClientConfig GoogleClientConfig::resolve(const GoogleClientConfig &builtIn,
                                               const QProcessEnvironment &environment)
{
    // One client's id with another's secret never authenticates, so the
    // environment cannot override half of the pair.
    const QString id = environment.value(QStringLiteral("CALLIE_GOOGLE_CLIENT_ID"));
    if (!id.isEmpty())
        return {id, environment.value(QStringLiteral("CALLIE_GOOGLE_CLIENT_SECRET"))};
    return builtIn;
}

GoogleClientConfig GoogleClientConfig::resolve()
{
    return resolve(builtIn(), QProcessEnvironment::systemEnvironment());
}

} // namespace callie
