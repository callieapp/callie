#pragma once

#include "Palette.h"

#include <QColor>
#include <QString>

#include <optional>

namespace callie::cli {

/// Colors terminal output from the app's theme: its accent and danger colors,
/// and calendar colors fitted to it. Text keeps the terminal's own color, since
/// the terminal's background need not be the theme's.
class Paint
{
public:
    enum class Depth { None, Basic, Ansi256, True };

    /// What the terminal on `fd` shows: nothing for a pipe or with NO_COLOR,
    /// 24-bit when COLORTERM says so, 256 colors when TERM does, else the
    /// basic sixteen.
    [[nodiscard]] static Depth detect(int fd);

    /// Plain text.
    Paint() = default;
    Paint(Depth depth, Palette palette) : m_depth(depth), m_palette(std::move(palette)) {}

    [[nodiscard]] bool enabled() const { return m_depth != Depth::None && m_palette; }
    [[nodiscard]] QString bold(const QString &text) const { return styled("1", text); }
    [[nodiscard]] QString dim(const QString &text) const { return styled("2", text); }
    [[nodiscard]] QString strike(const QString &text) const { return styled("9", text); }
    [[nodiscard]] QString accent(const QString &text) const;
    [[nodiscard]] QString danger(const QString &text) const;
    /// `text` in the calendar's sticker color.
    [[nodiscard]] QString calendar(const QColor &color, const QString &text) const;

    /// The escape sequence that sets `color` as the foreground, at `depth`.
    [[nodiscard]] static QString foreground(const QColor &color, Depth depth);

private:
    [[nodiscard]] QString styled(const char *code, const QString &text) const;
    [[nodiscard]] QString colored(const QColor &color, const QString &text) const;

    Depth m_depth = Depth::None;
    std::optional<Palette> m_palette;
};

} // namespace callie::cli
