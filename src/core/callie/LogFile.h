#pragma once

#include <QString>

namespace callie::logfile {

/// $XDG_STATE_HOME/callie/logs
[[nodiscard]] QString defaultDirectory();

/// Logs to `<directory>/<name>.log` as well as the terminal, so a problem can be
/// read after the app has closed. Returns false if the file cannot be opened.
bool install(const QString &name, const QString &directory = defaultDirectory(),
             qint64 maxBytes = 1024 * 1024);

/// Shows info messages in the terminal too, as `callie -v` asks.
void setVerboseTerminal(bool verbose);

/// The file install() opened, or empty.
[[nodiscard]] QString path();

/// Restores Qt's own handler and closes the file. For tests.
void uninstall();

} // namespace callie::logfile
