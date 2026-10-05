#pragma once

#include <QProcessEnvironment>
#include <QString>

namespace callie {

/// The OAuth client Callie identifies itself as. Google treats a Desktop app
/// client secret as non-confidential, but it is still kept out of the repo and
/// injected at build time.
struct GoogleClientConfig
{
    QString clientId;
    QString clientSecret;

    [[nodiscard]] bool isValid() const { return !clientId.isEmpty() && !clientSecret.isEmpty(); }

    /// The client baked in by CMake, possibly empty.
    [[nodiscard]] static GoogleClientConfig builtIn();

    /// CALLIE_GOOGLE_CLIENT_ID and CALLIE_GOOGLE_CLIENT_SECRET, when the id is
    /// set, replace the built-in client as a pair.
    [[nodiscard]] static GoogleClientConfig resolve(const GoogleClientConfig &builtIn,
                                                    const QProcessEnvironment &environment);
    [[nodiscard]] static GoogleClientConfig resolve();
};

} // namespace callie
