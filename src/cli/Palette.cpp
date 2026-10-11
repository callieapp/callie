#include "Palette.h"

#include "callie/Color.h"
#include "callie/ThemeLoader.h"

namespace callie::cli {

Palette Palette::forTheme(const QString &idOrPath)
{
    if (!idOrPath.isEmpty()) {
        const ThemeLoadResult loaded = ThemeLoader::load(idOrPath);
        if (loaded.ok())
            return Palette(loaded.theme);
    }
    return Palette(ThemeLoader::defaultTheme());
}

QColor Palette::fill(const QColor &calendar) const
{
    const ThemeSpec::Calendar &c = m_theme.calendar;
    return c.harmonize ? color::harmonize(calendar, c.lightness, c.chroma) : calendar;
}

} // namespace callie::cli
