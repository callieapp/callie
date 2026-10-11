#pragma once

#include "callie/ThemeSpec.h"

#include <QColor>
#include <QString>

namespace callie::cli {

/// The app's theme for the terminal: its colors, and calendar colors fitted to
/// it the way the app draws its event stickers.
class Palette
{
public:
    explicit Palette(const ThemeSpec &theme) : m_theme(theme) {}

    /// The theme saved in settings, `idOrPath`, or the default when it is
    /// empty or does not load.
    [[nodiscard]] static Palette forTheme(const QString &idOrPath);

    [[nodiscard]] const ThemeSpec::Colors &colors() const { return m_theme.colors; }
    /// A calendar's color as the app shows it with this theme.
    [[nodiscard]] QColor fill(const QColor &calendar) const;
    /// The text color that reads on that fill, for an event drawn as a sticker.
    [[nodiscard]] QColor ink(const QColor &calendar) const;

private:
    ThemeSpec m_theme;
};

} // namespace callie::cli
