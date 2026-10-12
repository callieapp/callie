#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <optional>

namespace callie::base16 {

/// A Callie theme, as TOML, from a base16 or base24 scheme in the classic
/// layout or tinted-theming's, with colors nudged where text would not read.
/// Empty, with `error` set, when the scheme lacks a name or a color.
[[nodiscard]] std::optional<QByteArray> toTheme(QByteArrayView yaml, QString *error);

/// Whether a file is a base16 scheme rather than a Callie theme, by its name.
[[nodiscard]] bool isScheme(const QString &path);

} // namespace callie::base16
