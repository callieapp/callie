#pragma once

#include <QProcessEnvironment>
#include <QString>

namespace callie {

/// The Microsoft Entra app Callie signs in as. It is a public client, signing
/// in with PKCE, so it has an id and no secret.
struct MicrosoftClientConfig
{
    QString clientId;

    [[nodiscard]] bool isValid() const { return !clientId.isEmpty(); }

    /// The app baked in by CMake, possibly empty.
    [[nodiscard]] static MicrosoftClientConfig builtIn();

    /// CALLIE_MICROSOFT_CLIENT_ID, when set, replaces the built-in app.
    [[nodiscard]] static MicrosoftClientConfig resolve(const MicrosoftClientConfig &builtIn,
                                                       const QProcessEnvironment &environment);
    [[nodiscard]] static MicrosoftClientConfig resolve();
};

} // namespace callie
