#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <optional>

namespace callie::base16 {

/// A Callie theme, as TOML, made from a base16 or base24 scheme in either the
/// classic YAML layout (`scheme:` and top-level `base00:`) or tinted-theming's
/// (`name:`, `variant:` and a `palette:`). Colors are nudged where needed so
/// text reads on the backgrounds. Empty, with `error` set, when the scheme
/// lacks a name or any of base00 to base0F.
[[nodiscard]] std::optional<QByteArray> toTheme(QByteArrayView yaml, QString *error);

/// Whether a file is a base16 scheme rather than a Callie theme, by its name.
[[nodiscard]] bool isScheme(const QString &path);

} // namespace callie::base16
