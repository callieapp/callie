#include "callie/ThemeLoader.h"

#include "callie/Color.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>

#include <toml++/toml.hpp>

#include <algorithm>
#include <functional>

namespace callie {

namespace {

const QString kBuiltInDir = QStringLiteral(":/callie/themes");

using Setter =
    std::function<void(ThemeSpec &, const toml::node &, const QString &key, QStringList &errors)>;

template<typename Field>
Setter colorSetter(Field field)
{
    return
        [field](ThemeSpec &theme, const toml::node &node, const QString &key, QStringList &errors) {
            const auto text = node.value_exact<std::string>();
            const auto color = text ? color::parseHex(QString::fromStdString(*text)) : std::nullopt;
            if (!color) {
                errors << QStringLiteral("%1: expected a color like \"#rrggbb\"").arg(key);
                return;
            }
            field(theme) = *color;
        };
}

template<typename Field>
Setter integerSetter(Field field, int min, int max)
{
    return [=](ThemeSpec &theme, const toml::node &node, const QString &key, QStringList &errors) {
        const auto value = node.value_exact<int64_t>();
        if (!value || *value < min || *value > max) {
            errors << QStringLiteral("%1: expected a whole number from %2 to %3")
                          .arg(key)
                          .arg(min)
                          .arg(max);
            return;
        }
        field(theme) = int(*value);
    };
}

template<typename Field>
Setter numberSetter(Field field, double min, double max)
{
    return [=](ThemeSpec &theme, const toml::node &node, const QString &key, QStringList &errors) {
        const auto value = node.is_boolean() ? std::nullopt : node.value<double>();
        // Written so NaN, which compares false with everything, is rejected.
        if (!value || !(*value >= min && *value <= max)) {
            errors
                << QStringLiteral("%1: expected a number from %2 to %3").arg(key).arg(min).arg(max);
            return;
        }
        field(theme) = *value;
    };
}

template<typename Field>
Setter stringSetter(Field field)
{
    return
        [field](ThemeSpec &theme, const toml::node &node, const QString &key, QStringList &errors) {
            const auto value = node.value_exact<std::string>();
            if (!value) {
                errors << QStringLiteral("%1: expected a string").arg(key);
                return;
            }
            field(theme) = QString::fromStdString(*value);
        };
}

template<typename Field>
Setter booleanSetter(Field field)
{
    return
        [field](ThemeSpec &theme, const toml::node &node, const QString &key, QStringList &errors) {
            const auto value = node.value_exact<bool>();
            if (!value) {
                errors << QStringLiteral("%1: expected true or false").arg(key);
                return;
            }
            field(theme) = *value;
        };
}

void setVariant(ThemeSpec &theme, const toml::node &node, const QString &key, QStringList &errors)
{
    const auto value = node.value_exact<std::string>();
    if (!value || (*value != "dark" && *value != "light")) {
        errors << QStringLiteral("%1: expected \"dark\" or \"light\"").arg(key);
        return;
    }
    theme.dark = *value == "dark";
}

#define CALLIE_FIELD(path) [](ThemeSpec &t) -> auto & { return t.path; }

const QHash<QString, Setter> &setters()
{
    static const QHash<QString, Setter> table{
        {QStringLiteral("theme.name"), stringSetter(CALLIE_FIELD(name))},
        {QStringLiteral("theme.variant"), setVariant},
        {QStringLiteral("colors.background"), colorSetter(CALLIE_FIELD(colors.background))},
        {QStringLiteral("colors.surface"), colorSetter(CALLIE_FIELD(colors.surface))},
        {QStringLiteral("colors.surface-alt"), colorSetter(CALLIE_FIELD(colors.surfaceAlt))},
        {QStringLiteral("colors.hairline"), colorSetter(CALLIE_FIELD(colors.hairline))},
        {QStringLiteral("colors.border"), colorSetter(CALLIE_FIELD(colors.border))},
        {QStringLiteral("colors.text"), colorSetter(CALLIE_FIELD(colors.text))},
        {QStringLiteral("colors.text-muted"), colorSetter(CALLIE_FIELD(colors.textMuted))},
        {QStringLiteral("colors.text-faint"), colorSetter(CALLIE_FIELD(colors.textFaint))},
        {QStringLiteral("colors.accent"), colorSetter(CALLIE_FIELD(colors.accent))},
        {QStringLiteral("colors.accent-text"), colorSetter(CALLIE_FIELD(colors.accentText))},
        {QStringLiteral("colors.danger"), colorSetter(CALLIE_FIELD(colors.danger))},
        {QStringLiteral("colors.edge"), colorSetter(CALLIE_FIELD(colors.edge))},
        {QStringLiteral("colors.accent-edge"), colorSetter(CALLIE_FIELD(colors.accentEdge))},
        {QStringLiteral("calendar.harmonize"), booleanSetter(CALLIE_FIELD(calendar.harmonize))},
        {QStringLiteral("calendar.lightness"),
         numberSetter(CALLIE_FIELD(calendar.lightness), 0, 1)},
        {QStringLiteral("calendar.chroma"), numberSetter(CALLIE_FIELD(calendar.chroma), 0, 0.4)},
        {QStringLiteral("calendar.ink-lightness"),
         numberSetter(CALLIE_FIELD(calendar.inkLightness), 0, 1)},
        {QStringLiteral("calendar.ink-chroma"),
         numberSetter(CALLIE_FIELD(calendar.inkChroma), 0, 0.4)},
        {QStringLiteral("calendar.edge-lightness"),
         numberSetter(CALLIE_FIELD(calendar.edgeLightness), 0, 1)},
        {QStringLiteral("calendar.edge-chroma"),
         numberSetter(CALLIE_FIELD(calendar.edgeChroma), 0, 0.4)},
        {QStringLiteral("shape.radius-small"),
         integerSetter(CALLIE_FIELD(shape.radiusSmall), 0, 64)},
        {QStringLiteral("shape.radius"), integerSetter(CALLIE_FIELD(shape.radius), 0, 64)},
        {QStringLiteral("shape.radius-large"),
         integerSetter(CALLIE_FIELD(shape.radiusLarge), 0, 64)},
        {QStringLiteral("shape.radius-xlarge"),
         integerSetter(CALLIE_FIELD(shape.radiusXLarge), 0, 64)},
        {QStringLiteral("shape.sticker-edge"),
         integerSetter(CALLIE_FIELD(shape.stickerEdge), 0, 8)},
        {QStringLiteral("shadow.color"), colorSetter(CALLIE_FIELD(shadow.color))},
        {QStringLiteral("shadow.opacity"), numberSetter(CALLIE_FIELD(shadow.opacity), 0, 1)},
        {QStringLiteral("shadow.blur"), integerSetter(CALLIE_FIELD(shadow.blur), 0, 128)},
        {QStringLiteral("shadow.offset"), integerSetter(CALLIE_FIELD(shadow.offset), 0, 64)},
        {QStringLiteral("type.family"), stringSetter(CALLIE_FIELD(type.family))},
        {QStringLiteral("type.display-family"), stringSetter(CALLIE_FIELD(type.displayFamily))},
        {QStringLiteral("type.mono-family"), stringSetter(CALLIE_FIELD(type.monoFamily))},
        {QStringLiteral("motion.duration-fast"),
         integerSetter(CALLIE_FIELD(motion.durationFast), 0, 2000)},
        {QStringLiteral("motion.duration"), integerSetter(CALLIE_FIELD(motion.duration), 0, 2000)},
        {QStringLiteral("motion.duration-slow"),
         integerSetter(CALLIE_FIELD(motion.durationSlow), 0, 2000)},
        {QStringLiteral("motion.bounce"), numberSetter(CALLIE_FIELD(motion.bounce), 0, 3)},
    };
    return table;
}

#undef CALLIE_FIELD

void requireColors(const ThemeSpec &theme, QStringList &errors)
{
    const std::pair<const char *, QColor> colors[] = {
        {"background", theme.colors.background},  {"surface", theme.colors.surface},
        {"surface-alt", theme.colors.surfaceAlt}, {"hairline", theme.colors.hairline},
        {"border", theme.colors.border},          {"text", theme.colors.text},
        {"text-muted", theme.colors.textMuted},   {"text-faint", theme.colors.textFaint},
        {"accent", theme.colors.accent},          {"accent-text", theme.colors.accentText},
        {"danger", theme.colors.danger},          {"edge", theme.colors.edge},
        {"accent-edge", theme.colors.accentEdge},
    };
    for (const auto &[name, color] : colors) {
        if (!color.isValid())
            errors << QStringLiteral("colors.%1: missing").arg(QLatin1String(name));
    }
}

ThemeLoadResult readFile(const QString &path, const ThemeSpec &base)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        ThemeLoadResult result{base, {}, {}};
        result.errors << QStringLiteral("cannot read %1: %2").arg(path, file.errorString());
        return result;
    }
    return ThemeLoader::parse(file.readAll(), base);
}

} // namespace

ThemeLoadResult ThemeLoader::parse(QByteArrayView toml, const ThemeSpec &base)
{
    ThemeLoadResult result{base, {}, {}};

    toml::table root;
    try {
        root = toml::parse(std::string_view(toml.data(), size_t(toml.size())));
    } catch (const toml::parse_error &error) {
        result.errors << QStringLiteral("line %1: %2")
                             .arg(error.source().begin.line)
                             .arg(QString::fromUtf8(error.description().data(),
                                                    qsizetype(error.description().size())));
        return result;
    }

    for (const auto &[sectionKey, section] : root) {
        const QString sectionName =
            QString::fromUtf8(sectionKey.str().data(), qsizetype(sectionKey.str().size()));
        const toml::table *table = section.as_table();
        if (!table) {
            result.warnings << QStringLiteral("%1: unknown setting, ignored").arg(sectionName);
            continue;
        }
        for (const auto &[key, node] : *table) {
            const QString path = sectionName + u'.' +
                                 QString::fromUtf8(key.str().data(), qsizetype(key.str().size()));
            const auto setter = setters().constFind(path);
            if (setter == setters().cend())
                result.warnings << QStringLiteral("%1: unknown setting, ignored").arg(path);
            else
                (*setter)(result.theme, node, path, result.errors);
        }
    }

    requireColors(result.theme, result.errors);
    if (result.ok())
        result.warnings << contrastWarnings(result.theme);
    return result;
}

ThemeLoadResult ThemeLoader::loadFile(const QString &path, const ThemeSpec &base)
{
    return readFile(path, base);
}

QStringList ThemeLoader::builtInIds()
{
    QStringList ids;
    for (const QFileInfo &file : QDir(kBuiltInDir).entryInfoList({QStringLiteral("*.toml")}))
        ids << file.completeBaseName();
    return ids;
}

const ThemeSpec &ThemeLoader::defaultTheme()
{
    static const ThemeSpec theme =
        readFile(kBuiltInDir + QStringLiteral("/callie.toml"), ThemeSpec{}).theme;
    return theme;
}

ThemeLoadResult ThemeLoader::loadBuiltIn(const QString &id)
{
    const QString path = kBuiltInDir + u'/' + id + QStringLiteral(".toml");
    if (!QFileInfo::exists(path)) {
        ThemeLoadResult result{defaultTheme(), {}, {}};
        result.errors << QStringLiteral("no built-in theme named '%1'").arg(id);
        return result;
    }
    return readFile(path, defaultTheme());
}

bool ThemeLoader::isPath(const QString &idOrPath)
{
    return idOrPath.contains(u'/') || idOrPath.endsWith(QStringLiteral(".toml"));
}

ThemeLoadResult ThemeLoader::load(const QString &idOrPath)
{
    if (isPath(idOrPath))
        return loadFile(idOrPath, defaultTheme());
    return loadBuiltIn(idOrPath);
}

QStringList ThemeLoader::contrastWarnings(const ThemeSpec &theme)
{
    struct Pair
    {
        const char *foreground;
        QColor fg;
        const char *background;
        QColor bg;
        double minimum;
    };
    const ThemeSpec::Colors &c = theme.colors;
    // 4.5:1 is WCAG AA for body text, 3:1 for large text and interface marks.
    const Pair pairs[] = {
        {"text", c.text, "background", c.background, 4.5},
        {"text", c.text, "surface", c.surface, 4.5},
        {"text", c.text, "surface-alt", c.surfaceAlt, 4.5},
        {"text-muted", c.textMuted, "background", c.background, 4.5},
        {"text-muted", c.textMuted, "surface", c.surface, 4.5},
        {"text-faint", c.textFaint, "background", c.background, 3.0},
        {"accent", c.accent, "background", c.background, 3.0},
        {"accent-text", c.accentText, "accent", c.accent, 4.5},
    };

    QStringList warnings;
    // Event titles sit in the ink color on the sticker fill, at every hue.
    if (theme.calendar.harmonize) {
        double worst = 21;
        for (int hue = 0; hue < 360; hue += 15) {
            const QColor fill =
                color::fromOklch({theme.calendar.lightness, theme.calendar.chroma, double(hue)});
            const QColor ink = color::fromOklch(
                {theme.calendar.inkLightness, theme.calendar.inkChroma, double(hue)});
            worst = std::min(worst, color::contrastRatio(ink, fill));
        }
        if (worst < 4.5)
            warnings << QStringLiteral("calendar ink on its fill is %1:1 at some hues, below 4.5:1")
                            .arg(worst, 0, 'f', 1);
    }
    for (const Pair &pair : pairs) {
        const double ratio = color::contrastRatio(pair.fg, pair.bg);
        if (ratio < pair.minimum)
            warnings << QStringLiteral("%1 on %2 is %3:1, below %4:1")
                            .arg(QLatin1String(pair.foreground), QLatin1String(pair.background))
                            .arg(ratio, 0, 'f', 1)
                            .arg(pair.minimum, 0, 'f', 1);
    }
    return warnings;
}

} // namespace callie
