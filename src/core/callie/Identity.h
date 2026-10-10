#pragma once

#include <QString>

/// Who this build of Callie is. A development build (CALLIE_DEVEL) has its own
/// app id, folders and keyring entries, so it runs beside an installed Callie
/// without sharing its accounts, cache or settings.
namespace callie::identity {

/// "org.callieapp.Callie", or "org.callieapp.Callie.Devel" for a development build.
[[nodiscard]] QString appId();
/// The folder under each XDG directory: "callie" or "callie-devel".
[[nodiscard]] QString dirName();
/// "Callie" or "Callie Devel", for what the desktop shows.
[[nodiscard]] QString displayName();
[[nodiscard]] bool isDevel();

} // namespace callie::identity
