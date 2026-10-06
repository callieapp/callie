#pragma once

#include "ThemeSpec.h"

#include <QString>

namespace callie {

/// The page the browser shows when Google hands the sign-in back to Callie,
/// drawn in `theme`'s colors. It is body content: the reply handler wraps it
/// in its own html and head.
[[nodiscard]] QString signInPage(const ThemeSpec &theme);

} // namespace callie
