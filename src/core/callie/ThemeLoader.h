#pragma once

#include "ThemeSpec.h"

#include <QByteArrayView>
#include <QStringList>

namespace callie {

struct ThemeLoadResult
{
    ThemeSpec theme;
    QStringList errors;   // the theme must not be applied
    QStringList warnings; // the theme works, but something deserves attention

    [[nodiscard]] bool ok() const { return errors.isEmpty(); }
};

/// Reads TOML theme files. A theme only lists what it changes; everything else
/// comes from the base it is parsed over, normally the built-in default.
class ThemeLoader
{
public:
    [[nodiscard]] static ThemeLoadResult parse(QByteArrayView toml, const ThemeSpec &base);
    [[nodiscard]] static ThemeLoadResult loadFile(const QString &path, const ThemeSpec &base);

    [[nodiscard]] static QStringList builtInIds();
    [[nodiscard]] static ThemeLoadResult loadBuiltIn(const QString &id);

    /// The built-in default, which every other theme is parsed over.
    [[nodiscard]] static const ThemeSpec &defaultTheme();

    /// Whether `idOrPath` names a theme file rather than a built-in id.
    [[nodiscard]] static bool isPath(const QString &idOrPath);

    /// Loads an installed theme by id, or a theme file by path, over the default.
    [[nodiscard]] static ThemeLoadResult load(const QString &idOrPath);

    /// Pairs that fall below WCAG contrast targets, as human-readable warnings.
    [[nodiscard]] static QStringList contrastWarnings(const ThemeSpec &theme);
};

} // namespace callie
