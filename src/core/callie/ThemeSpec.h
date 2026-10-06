#pragma once

#include <QColor>
#include <QString>

namespace callie {

/// Every value a theme can set. Spacing, type sizes and grid metrics are product
/// decisions and deliberately absent; see DESIGN.md.
struct ThemeSpec
{
    struct Colors
    {
        QColor background, surface, surfaceAlt, hairline, border;
        QColor text, textMuted, textFaint;
        QColor accent, accentText, danger;
        QColor edge;       // the darker strip under a raised button
        QColor accentEdge; // the same under an accent-colored one
    };

    /// A calendar color becomes a sticker: a fill, the ink drawn on it and the
    /// edge under it, each the calendar's hue at its own OKLCH lightness and chroma.
    struct Calendar
    {
        bool harmonize = true;
        double lightness = 0.72;
        double chroma = 0.12;
        double inkLightness = 0.25;
        double inkChroma = 0.05;
        double edgeLightness = 0.58;
        double edgeChroma = 0.09;
    };

    struct Shape
    {
        int radiusSmall = 3;
        int radius = 4;
        int radiusLarge = 8;
        int radiusXLarge = 12;
        int stickerEdge = 3; // px of edge showing under raised things; 0 is flat
    };

    /// The soft shadow under menus, popovers and tooltips.
    struct Shadow
    {
        QColor color = QColor(0, 0, 0);
        double opacity = 0.45;
        int blur = 32;
        int offset = 12; // downwards, px
    };

    struct Type
    {
        QString family; // empty means the system font
        QString displayFamily;
        QString monoFamily;
    };

    struct Motion
    {
        int durationFast = 140; // milliseconds
        int duration = 200;
        int durationSlow = 280;
        double bounce = 1.2; // overshoot when something arrives; 0 disables it
    };

    QString name;
    bool dark = true;
    Colors colors;
    Calendar calendar;
    Shape shape;
    Shadow shadow;
    Type type;
    Motion motion;
};

} // namespace callie
