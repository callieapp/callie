#pragma once

#include <QString>

namespace callie {
class Settings;
}

namespace callie::cli {

class Palette;
class TuiState;

/// Runs the TUI until it is quit, and gives the exit code.
int runTui(TuiState &state, const Settings &settings, const Palette &palette);

/// The TUI as it draws `state`, at `width` by `height` cells, without its
/// colors; for tests.
[[nodiscard]] QString renderTui(const TuiState &state, const Settings &settings,
                                const Palette &palette, int width, int height);

} // namespace callie::cli
