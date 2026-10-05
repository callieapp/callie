#pragma once

#include <QColor>
#include <QStringView>

#include <optional>

/// Color math for themes. OKLCH is used because equal steps in it look equal,
/// which is what lets calendar colors be re-lit without changing their identity.
namespace callie::color {

struct Oklch
{
    double lightness = 0; // 0 to 1
    double chroma = 0;    // 0 to about 0.37 within sRGB
    double hue = 0;       // degrees
};

[[nodiscard]] Oklch toOklch(const QColor &color);

/// Reduces chroma until the color fits sRGB, so the hue is never distorted.
[[nodiscard]] QColor fromOklch(const Oklch &color, qreal alpha = 1.0);

/// WCAG 2 contrast ratio, from 1 to 21.
[[nodiscard]] double contrastRatio(const QColor &a, const QColor &b);

/// Keeps the source's hue and replaces its lightness and chroma. Near-greys stay
/// grey, since their hue carries no meaning.
[[nodiscard]] QColor harmonize(const QColor &source, double lightness, double chroma);

/// `#rrggbb` or `#rrggbbaa`, with alpha last as in CSS.
[[nodiscard]] std::optional<QColor> parseHex(QStringView text);

} // namespace callie::color
