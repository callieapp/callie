#pragma once

#include <QString>

namespace callie::logfile {

/// $XDG_STATE_HOME/callie/logs
[[nodiscard]] QString defaultDirectory();

/// Copies every Qt message into `<directory>/<name>.log`, which is rotated to
/// `.1` .. `.3` once it passes `maxBytes`. The terminal keeps showing warnings
/// only, or everything enabled when QT_LOGGING_RULES is set. Returns false,
/// leaving logging to the terminal, when the file cannot be opened.
bool install(const QString &name, const QString &directory = defaultDirectory(),
             qint64 maxBytes = 1024 * 1024);

/// The file install() opened, or empty.
[[nodiscard]] QString path();

/// Restores Qt's own handler and closes the file. For tests.
void uninstall();

} // namespace callie::logfile
