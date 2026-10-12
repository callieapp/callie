#include "callie/Base16.h"

#include "callie/Color.h"

#include <QFileInfo>
#include <QHash>
#include <QObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <initializer_list>

using namespace Qt::StringLiterals;

namespace callie::base16 {

namespace {

/// The `key: value` pairs of a scheme, nested ones included under their own
/// key, which is all either layout needs.
QHash<QString, QString> fields(QByteArrayView yaml)
{
    static const QRegularExpression pair(u"^\\s*([A-Za-z0-9_-]+)\\s*:\\s*(.*)$"_s);
    QHash<QString, QString> found;
    for (const QString &line : QString::fromUtf8(yaml).split(u'\n')) {
        const QRegularExpressionMatch match = pair.match(line);
        if (!match.hasMatch())
            continue;
        QString value = match.captured(2).trimmed();
        if (value.startsWith(u'"') || value.startsWith(u'\'')) {
            const qsizetype close = value.indexOf(value.front(), 1);
            value = value.mid(1, close < 0 ? -1 : close - 1);
        } else {
            const qsizetype comment = value.indexOf(u" #"_s);
            if (comment >= 0)
                value.truncate(comment);
            // Strictly a comment, but a bare "#1d1f21" is meant as a color, and
            // a mistyped one should be reported as such.
            if (value == u"#" || value.startsWith(u"# "_s))
                value.clear();
            value = value.trimmed();
        }
        found.insert(match.captured(1), value);
    }
    return found;
}

QColor mix(const QColor &a, const QColor &b, double t)
{
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t),
                            float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

/// `color`, moved toward `away` only as far as it takes to reach `ratio`
/// against every one of `backgrounds`.
QColor readable(const QColor &color, const QColor &away, double ratio,
                std::initializer_list<QColor> backgrounds)
{
    for (int step = 0; step <= 20; ++step) {
        const QColor tried = mix(color, away, step / 20.0);
        if (std::ranges::all_of(backgrounds, [&](const QColor &background) {
                return color::contrastRatio(tried, background) >= ratio;
            }))
            return tried;
    }
    return away;
}

/// A TOML basic string.
QString quoted(QString text)
{
    text.replace(u'\\', u"\\\\"_s).replace(u'"', u"\\\""_s);
    text.remove(QRegularExpression(u"[\\x00-\\x1f]"_s));
    return u'"' + text + u'"';
}

} // namespace

bool isScheme(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == u"yaml" || suffix == u"yml";
}

std::optional<QByteArray> toTheme(QByteArrayView yaml, QString *error)
{
    const QHash<QString, QString> scheme = fields(yaml);
    const QString name = scheme.value(u"name"_s, scheme.value(u"scheme"_s)).trimmed();
    if (name.isEmpty()) {
        *error = QObject::tr("the base16 scheme has no name");
        return std::nullopt;
    }
    QColor base[16];
    for (int i = 0; i < 16; ++i) {
        const QString key = u"base0"_s + QString::number(i, 16).toUpper();
        const QString value = scheme.value(key, scheme.value(key.toLower()));
        if (value.isEmpty()) {
            *error = QObject::tr("the base16 scheme has no color for %1").arg(key);
            return std::nullopt;
        }
        const std::optional<QColor> parsed =
            color::parseHex(value.startsWith(u'#') ? value : u"#"_s + value);
        if (!parsed) {
            *error = QObject::tr("the base16 scheme's %1, %2, is not a color").arg(key, value);
            return std::nullopt;
        }
        base[i] = *parsed;
    }

    const QString variant = scheme.value(u"variant"_s).toLower();
    const bool dark =
        variant == u"dark" || (variant != u"light" && color::toOklch(base[0]).lightness < 0.5);
    const QColor black(Qt::black);
    const QColor white(Qt::white);
    const QColor away = dark ? white : black;

    const QColor background = base[0];
    const QColor surface = base[1];
    // Some schemes' selection color, base02, is far from their backgrounds, so
    // the raised surface only takes a step toward it.
    const double apart =
        std::abs(color::toOklch(base[2]).lightness - color::toOklch(base[1]).lightness);
    const QColor surfaceAlt = apart <= 0.06 ? base[2] : mix(base[1], base[2], 0.06 / apart);
    const QColor text = readable(base[5], away, 4.5, {background, surface, surfaceAlt});
    const QColor muted = readable(base[4], away, 4.5, {background, surface});
    const QColor faint = readable(base[3], away, 3.0, {background});
    // Magenta, base0E, is the scheme's own accent in most of its uses.
    const QColor accent = readable(base[14], away, 3.0, {background});
    const QColor onAccent =
        color::contrastRatio(base[0], accent) >= color::contrastRatio(base[7], accent) ? base[0]
                                                                                       : base[7];
    const QColor accentText = readable(
        onAccent,
        color::contrastRatio(black, accent) >= color::contrastRatio(white, accent) ? black : white,
        4.5, {accent});
    const QColor danger = readable(base[8], away, 3.0, {background});

    QStringList lines;
    const auto set = [&lines](const QString &key, const QColor &value) {
        lines << u"%1 = \"%2\""_s.arg(key, value.name());
    };
    lines << u"# Made from the base16 scheme %1%2. Anything not set here comes from callie.toml."_s
                 .arg(name.simplified(), scheme.value(u"author"_s).isEmpty()
                                             ? QString()
                                             : u" by "_s + scheme.value(u"author"_s).simplified())
          << QString() << u"[theme]"_s << u"name = "_s + quoted(name)
          << u"variant = \"%1\""_s.arg(dark ? u"dark"_s : u"light"_s) << QString() << u"[colors]"_s;
    set(u"background"_s, background);
    set(u"surface"_s, surface);
    set(u"surface-alt"_s, surfaceAlt);
    set(u"hairline"_s, mix(background, surfaceAlt, 0.5));
    set(u"border"_s, mix(surfaceAlt, base[3], 0.35));
    set(u"text"_s, text);
    set(u"text-muted"_s, muted);
    set(u"text-faint"_s, faint);
    set(u"accent"_s, accent);
    set(u"accent-text"_s, accentText);
    set(u"danger"_s, danger);
    set(u"edge"_s, dark ? mix(background, black, 0.35) : mix(surfaceAlt, base[3], 0.5));
    set(u"accent-edge"_s, mix(accent, black, 0.3));
    // Light schemes take Callie Light's stickers and shadow; dark ones keep Callie's.
    if (!dark) {
        lines << QString() << u"[calendar]"_s << u"lightness = 0.88"_s << u"chroma = 0.07"_s
              << u"ink-lightness = 0.3"_s << u"ink-chroma = 0.07"_s << u"edge-lightness = 0.72"_s
              << u"edge-chroma = 0.08"_s << QString() << u"[shadow]"_s;
        set(u"color"_s, base[3]);
        lines << u"opacity = 0.18"_s << u"blur = 30"_s << u"offset = 10"_s;
    }
    return (lines.join(u'\n') + u'\n').toUtf8();
}

} // namespace callie::base16
