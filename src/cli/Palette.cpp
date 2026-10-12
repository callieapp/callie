#include "Palette.h"

#include "callie/Color.h"
#include "callie/ThemeLoader.h"

#include <algorithm>

namespace callie::cli {

namespace {

/// Lightness, in OKLCH, that reads at 4.5:1 on a near-white page at every hue.
constexpr double kOnLightPage = 0.5;

} // namespace

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

QColor Palette::ink(const QColor &calendar) const
{
    const ThemeSpec::Calendar &c = m_theme.calendar;
    if (c.harmonize)
        return color::harmonize(calendar, c.inkLightness, c.inkChroma);
    // As the app does for an unfitted color: whichever of black and white reads.
    const QColor black(0, 0, 0), white(255, 255, 255);
    return color::contrastRatio(black, calendar) >= color::contrastRatio(white, calendar) ? black
                                                                                          : white;
}

QColor Palette::onPage(const QColor &calendar) const
{
    const ThemeSpec::Calendar &c = m_theme.calendar;
    if (m_theme.dark || !c.harmonize)
        return fill(calendar);
    return color::harmonize(calendar, kOnLightPage, std::max(c.chroma, c.edgeChroma));
}

} // namespace callie::cli
