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
    };

    struct Calendar
    {
        bool harmonize = true;
        double lightness = 0.72; // OKLCH
        double chroma = 0.12;    // OKLCH
    };

    struct Shape
    {
        int radiusSmall = 3;
        int radius = 4;
        int radiusLarge = 8;
        int radiusXLarge = 12;
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
    Type type;
    Motion motion;
};

} // namespace callie
