#include "Paint.h"

#include <QByteArray>

#include <array>

#include <unistd.h>

using namespace Qt::StringLiterals;

namespace callie::cli {

namespace {

const QString kReset = u"\033[0m"_s;

int distance(const QColor &a, int r, int g, int b)
{
    const int dr = a.red() - r, dg = a.green() - g, db = a.blue() - b;
    return dr * dr + dg * dg + db * db;
}

/// The nearest of xterm's 6x6x6 cube and its 24 greys.
int nearest256(const QColor &color)
{
    static constexpr std::array<int, 6> levels = {0, 95, 135, 175, 215, 255};
    const auto level = [](int value) {
        int best = 0;
        for (int i = 1; i < 6; ++i) {
            if (std::abs(levels[i] - value) < std::abs(levels[best] - value))
                best = i;
        }
        return best;
    };
    const int r = level(color.red()), g = level(color.green()), b = level(color.blue());
    const int cube = 16 + 36 * r + 6 * g + b;
    const int cubeDistance = distance(color, levels[r], levels[g], levels[b]);
    const int grey = std::clamp((color.red() + color.green() + color.blue()) / 3, 8, 238);
    const int step = std::clamp((grey - 8 + 5) / 10, 0, 23);
    const int greyValue = 8 + step * 10;
    return distance(color, greyValue, greyValue, greyValue) < cubeDistance ? 232 + step : cube;
}

/// The nearest of the six basic colors, red to cyan.
int nearestBasic(const QColor &color)
{
    static constexpr std::array<std::array<int, 3>, 6> basic = {{{205, 49, 49},
                                                                 {13, 188, 121},
                                                                 {229, 229, 16},
                                                                 {36, 114, 200},
                                                                 {188, 63, 188},
                                                                 {17, 168, 205}}};
    int best = 0;
    for (int i = 1; i < 6; ++i) {
        if (distance(color, basic[i][0], basic[i][1], basic[i][2]) <
            distance(color, basic[best][0], basic[best][1], basic[best][2]))
            best = i;
    }
    return 31 + best;
}

} // namespace

Paint::Depth Paint::detect(int fd)
{
    return depthFor(::isatty(fd), qgetenv("TERM"), qgetenv("COLORTERM"),
                    !qEnvironmentVariableIsEmpty("NO_COLOR"));
}

Paint::Depth Paint::depthFor(bool terminal, const QByteArray &term, const QByteArray &colorTerm,
                             bool noColor)
{
    if (noColor || !terminal || term == "dumb")
        return Depth::None;
    if (colorTerm == "truecolor" || colorTerm == "24bit")
        return Depth::True;
    return term.contains("256color") ? Depth::Ansi256 : Depth::Basic;
}

QString Paint::foreground(const QColor &color, Depth depth)
{
    switch (depth) {
    case Depth::True:
        return u"\033[38;2;%1;%2;%3m"_s.arg(color.red()).arg(color.green()).arg(color.blue());
    case Depth::Ansi256: return u"\033[38;5;%1m"_s.arg(nearest256(color));
    case Depth::Basic: return u"\033[%1m"_s.arg(nearestBasic(color));
    case Depth::None: break;
    }
    return {};
}

QString Paint::styled(const char *code, const QString &text) const
{
    return enabled() ? u"\033["_s + QLatin1StringView(code) + u'm' + text + kReset : text;
}

QString Paint::colored(const QColor &color, const QString &text) const
{
    return enabled() ? foreground(color, m_depth) + text + kReset : text;
}

QString Paint::accent(const QString &text) const
{
    return enabled() ? colored(m_palette->colors().accent, text) : text;
}

QString Paint::danger(const QString &text) const
{
    return enabled() ? colored(m_palette->colors().danger, text) : text;
}

QString Paint::calendar(const QColor &color, const QString &text) const
{
    return enabled() && color.isValid() ? colored(m_palette->onPage(color), text) : text;
}

} // namespace callie::cli
