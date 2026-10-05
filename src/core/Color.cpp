#include "callie/Color.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numbers>

namespace callie::color {

namespace {

double toLinear(double c)
{
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double toGamma(double c)
{
    return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1 / 2.4) - 0.055;
}

struct Rgb
{
    double r, g, b;
};

// Linear sRGB from OKLCH, per Bjorn Ottosson's reference matrices.
Rgb linearFromOklch(const Oklch &c)
{
    const double h = c.hue * std::numbers::pi / 180;
    const double a = c.chroma * std::cos(h);
    const double b = c.chroma * std::sin(h);
    const double l = std::pow(c.lightness + 0.3963377774 * a + 0.2158037573 * b, 3);
    const double m = std::pow(c.lightness - 0.1055613458 * a - 0.0638541728 * b, 3);
    const double s = std::pow(c.lightness - 0.0894841775 * a - 1.2914855480 * b, 3);
    return {4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
            -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
            -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s};
}

bool inGamut(const Rgb &c)
{
    constexpr double epsilon = 1e-6;
    return c.r >= -epsilon && c.r <= 1 + epsilon && c.g >= -epsilon && c.g <= 1 + epsilon &&
           c.b >= -epsilon && c.b <= 1 + epsilon;
}

double relativeLuminance(const QColor &c)
{
    return 0.2126 * toLinear(c.redF()) + 0.7152 * toLinear(c.greenF()) +
           0.0722 * toLinear(c.blueF());
}

} // namespace

Oklch toOklch(const QColor &color)
{
    const double r = toLinear(color.redF());
    const double g = toLinear(color.greenF());
    const double b = toLinear(color.blueF());
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    const double lightness = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    const double a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    const double bb = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
    double hue = std::atan2(bb, a) * 180 / std::numbers::pi;
    if (hue < 0)
        hue += 360;
    return {lightness, std::hypot(a, bb), hue};
}

QColor fromOklch(const Oklch &color, qreal alpha)
{
    Oklch c = color;
    c.lightness = std::clamp(c.lightness, 0.0, 1.0);
    Rgb rgb = linearFromOklch(c);
    if (!inGamut(rgb)) {
        double low = 0, high = c.chroma;
        for (int i = 0; i < 24; ++i) {
            c.chroma = (low + high) / 2;
            if (inGamut(linearFromOklch(c)))
                low = c.chroma;
            else
                high = c.chroma;
        }
        c.chroma = low;
        rgb = linearFromOklch(c);
    }
    const auto channel = [](double v) {
        return std::clamp(toGamma(std::clamp(v, 0.0, 1.0)), 0.0, 1.0);
    };
    return QColor::fromRgbF(float(channel(rgb.r)), float(channel(rgb.g)), float(channel(rgb.b)),
                            float(alpha));
}

double contrastRatio(const QColor &a, const QColor &b)
{
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QColor harmonize(const QColor &source, double lightness, double chroma)
{
    const Oklch original = toOklch(source);
    constexpr double greyChroma = 0.03;
    const double target = original.chroma < greyChroma ? original.chroma : chroma;
    return fromOklch({lightness, target, original.hue}, source.alphaF());
}

std::optional<QColor> parseHex(QStringView text)
{
    if (!text.startsWith(u'#') || (text.size() != 7 && text.size() != 9))
        return std::nullopt;
    // toInt() alone would accept a sign, so check the digits first.
    const auto isHex = [](QChar ch) { return std::isxdigit(ch.unicode()) && ch.unicode() < 128; };
    if (!std::all_of(text.begin() + 1, text.end(), isHex))
        return std::nullopt;
    int channels[4] = {0, 0, 0, 255};
    for (qsizetype i = 0; i < (text.size() - 1) / 2; ++i) {
        bool ok = false;
        channels[i] = text.sliced(1 + i * 2, 2).toInt(&ok, 16);
        if (!ok)
            return std::nullopt;
    }
    return QColor(channels[0], channels[1], channels[2], channels[3]);
}

} // namespace callie::color
