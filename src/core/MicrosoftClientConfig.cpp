#include "callie/MicrosoftClientConfig.h"

#include "callie/MicrosoftBuildConfig.h"

namespace callie {

MicrosoftClientConfig MicrosoftClientConfig::builtIn()
{
    return {QString::fromLatin1(build::microsoftClientId)};
}

MicrosoftClientConfig MicrosoftClientConfig::resolve(const MicrosoftClientConfig &builtIn,
                                                     const QProcessEnvironment &environment)
{
    const QString id = environment.value(QStringLiteral("CALLIE_MICROSOFT_CLIENT_ID"));
    return id.isEmpty() ? builtIn : MicrosoftClientConfig{id};
}

MicrosoftClientConfig MicrosoftClientConfig::resolve()
{
    return resolve(builtIn(), QProcessEnvironment::systemEnvironment());
}

} // namespace callie
