#pragma once

#include "ThemeSpec.h"

#include <QString>

namespace callie {

/// The page the browser shows when Google or Microsoft hands the sign-in back,
/// drawn in `theme`'s colors. It is body content: the reply handler wraps it
/// in its own html and head.
[[nodiscard]] QString signInPage(const ThemeSpec &theme);

} // namespace callie
