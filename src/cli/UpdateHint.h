#pragma once

#include "callie/UpdateCheck.h"

#include <QString>
#include <QVersionNumber>

namespace callie::cli {

/// What a command ends with about newer releases.
struct UpdateHint
{
    /// The line to print, or empty.
    QString line;
    /// GitHub is due to be asked, by a copy of callie left running on its own.
    bool ask = false;

    /// For `command`, run on a terminal or not, with the user's setting as it
    /// stands once the command has run, so turning it off is heeded at once.
    [[nodiscard]] static UpdateHint after(const QString &command, bool json, bool terminal,
                                          bool setting, const UpdateCheck &check,
                                          const QVersionNumber &running);
};

} // namespace callie::cli
