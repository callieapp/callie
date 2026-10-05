#pragma once

#include <QString>
#include <QUrl>

#include <functional>

namespace callie {

class TokenStore;

/// What a bug report needs: versions, environment, account and sync state,
/// and the end of each log. Email addresses are masked throughout.
namespace diagnostics {

/// Replaces each distinct email address with <email-1>, <email-2> and so on,
/// and the home folder with ~.
[[nodiscard]] QString redact(const QString &text);

/// Builds the report. Checking the keyring is asynchronous, so `done` is
/// called once, later. `extra` adds lines such as the GUI's platform plugin.
void collect(TokenStore &tokens, const QString &extra, std::function<void(QString)> done);

/// A new GitHub issue with `report` filled in, shortened to fit in a URL.
[[nodiscard]] QUrl issueUrl(const QString &report);

} // namespace diagnostics
} // namespace callie
