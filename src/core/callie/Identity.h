#pragma once

#include <QString>

/// Who this build of Callie is. A development build (CALLIE_DEVEL) has its own
/// app id, folders and keyring entries, so it runs beside an installed Callie
/// without sharing its accounts, cache or settings.
namespace callie::identity {

/// What a build is called, worked out from whether it is a development build.
struct Names
{
    /// "org.callieapp.Callie", or "org.callieapp.Callie.Devel".
    QString appId;
    /// The folder under each XDG directory: "callie" or "callie-devel".
    QString dirName;
    /// "Callie" or "Callie Devel", for what the desktop shows.
    QString displayName;
};
[[nodiscard]] Names namesFor(bool devel);

/// This build's names.
[[nodiscard]] QString appId();
[[nodiscard]] QString dirName();
[[nodiscard]] QString displayName();
[[nodiscard]] bool isDevel();
/// Whether Callie runs in a Flatpak sandbox, where Flathub delivers updates and
/// the host is reached through portals.
[[nodiscard]] bool isFlatpak();

} // namespace callie::identity
