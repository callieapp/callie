#include "ThemeController.h"

#include "callie/Color.h"
#include "callie/Logging.h"
#include "callie/ThemeLoader.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QTimer>

namespace callie {

ThemeController::ThemeController(QObject *parent)
    : QObject(parent), m_spec(ThemeLoader::defaultTheme()), m_watcher(new QFileSystemWatcher(this))
{
    // Bundled so the default theme looks the same everywhere; a theme may still
    // name any installed family.
    for (const char *font :
         {":/callie/fonts/Nunito-Variable.ttf", ":/callie/fonts/Fraunces-Variable.ttf"}) {
        if (QFontDatabase::addApplicationFont(QString::fromLatin1(font)) < 0)
            qCWarning(lcTheme) << "could not load the bundled font" << font;
    }
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
        // Editors often save by writing a new file and renaming it, which briefly
        // removes the watched path, so reload a moment later and re-watch.
        QTimer::singleShot(100, this, &ThemeController::reloadWatchedFile);
    });
}

ThemeController *ThemeController::instance()
{
    static ThemeController *theme = new ThemeController(QCoreApplication::instance());
    return theme;
}

ThemeController *ThemeController::create(QQmlEngine *, QJSEngine *)
{
    ThemeController *theme = instance();
    QJSEngine::setObjectOwnership(theme, QJSEngine::CppOwnership);
    return theme;
}

QStringList ThemeController::load(const QString &idOrPath)
{
    const ThemeLoadResult result = ThemeLoader::load(idOrPath);
    if (!result.ok())
        return result.errors;

    if (!m_watchedPath.isEmpty())
        m_watcher->removePath(m_watchedPath);
    m_watchedPath =
        ThemeLoader::isPath(idOrPath) ? QFileInfo(idOrPath).absoluteFilePath() : QString();
    if (!m_watchedPath.isEmpty())
        m_watcher->addPath(m_watchedPath);

    m_spec = result.theme;
    m_warnings = result.warnings;
    qCInfo(lcTheme) << "loaded theme" << m_spec.name << "from" << idOrPath;
    for (const QString &warning : m_warnings)
        qCInfo(lcTheme) << "theme warning:" << warning;
    Q_EMIT changed();
    return {};
}

void ThemeController::reloadWatchedFile()
{
    if (m_watchedPath.isEmpty())
        return;
    if (!m_watcher->files().contains(m_watchedPath) && QFileInfo::exists(m_watchedPath))
        m_watcher->addPath(m_watchedPath);

    const ThemeLoadResult result =
        ThemeLoader::loadFile(m_watchedPath, ThemeLoader::defaultTheme());
    if (!result.ok()) {
        // A half-typed edit should not wipe the look; keep the last good theme.
        for (const QString &error : result.errors)
            qCWarning(lcTheme).noquote() << m_watchedPath << "not applied:" << error;
        return;
    }
    m_spec = result.theme;
    m_warnings = result.warnings;
    qCInfo(lcTheme) << "reloaded theme" << m_spec.name;
    Q_EMIT changed();
}

QColor ThemeController::tint(const QColor &color, qreal alpha) const
{
    QColor tinted = color;
    tinted.setAlphaF(float(alpha));
    return tinted;
}

QColor ThemeController::calendarColor(const QColor &source, const QVariantMap &calendar) const
{
    if (!calendar.value(QStringLiteral("harmonize")).toBool())
        return source;
    return color::harmonize(source, calendar.value(QStringLiteral("lightness")).toDouble(),
                            calendar.value(QStringLiteral("chroma")).toDouble());
}

QColor ThemeController::calendarInk(const QColor &source, const QVariantMap &calendar) const
{
    if (!calendar.value(QStringLiteral("harmonize")).toBool()) {
        // An unfitted color can be anything, so pick whichever of black and
        // white reads better on it.
        const QColor black(0, 0, 0), white(255, 255, 255);
        return color::contrastRatio(black, source) >= color::contrastRatio(white, source) ? black
                                                                                          : white;
    }
    return color::harmonize(source, calendar.value(QStringLiteral("inkLightness")).toDouble(),
                            calendar.value(QStringLiteral("inkChroma")).toDouble());
}

QColor ThemeController::calendarEdge(const QColor &source, const QVariantMap &calendar) const
{
    if (!calendar.value(QStringLiteral("harmonize")).toBool())
        return source.darker(150);
    return color::harmonize(source, calendar.value(QStringLiteral("edgeLightness")).toDouble(),
                            calendar.value(QStringLiteral("edgeChroma")).toDouble());
}

QVariantMap ThemeController::calendar() const
{
    const ThemeSpec::Calendar &c = m_spec.calendar;
    return {{QStringLiteral("harmonize"), c.harmonize},
            {QStringLiteral("lightness"), c.lightness},
            {QStringLiteral("chroma"), c.chroma},
            {QStringLiteral("inkLightness"), c.inkLightness},
            {QStringLiteral("inkChroma"), c.inkChroma},
            {QStringLiteral("edgeLightness"), c.edgeLightness},
            {QStringLiteral("edgeChroma"), c.edgeChroma}};
}

QColor ThemeController::todayWash() const
{
    return tint(m_spec.colors.accent, 0.04);
}

QColor ThemeController::dangerEdge() const
{
    return m_spec.colors.danger.darker(140);
}

QColor ThemeController::dangerText() const
{
    // Whichever of the theme's own text and background reads better on the red.
    const QColor &red = m_spec.colors.danger;
    const QColor &text = m_spec.colors.text, &ground = m_spec.colors.background;
    return color::contrastRatio(text, red) >= color::contrastRatio(ground, red) ? text : ground;
}

QColor ThemeController::answerHover() const
{
    return tint(m_spec.colors.accent, 0.22);
}

QColor ThemeController::dayOffWash() const
{
    // Darker on a dark theme and on a light one alike.
    return tint(m_spec.colors.edge, m_spec.dark ? 0.35 : 0.06);
}

QColor ThemeController::shadowColor() const
{
    QColor shadow = m_spec.shadow.color;
    shadow.setAlphaF(float(m_spec.shadow.opacity));
    return shadow;
}

double ThemeController::contrast(const QColor &a, const QColor &b) const
{
    return color::contrastRatio(a, b);
}

QVariantList ThemeController::swatches() const
{
    const ThemeSpec::Colors &c = m_spec.colors;
    const std::pair<const char *, QColor> tokens[] = {
        {"background", c.background},  {"surface", c.surface},      {"surface-alt", c.surfaceAlt},
        {"hairline", c.hairline},      {"border", c.border},        {"text", c.text},
        {"text-muted", c.textMuted},   {"text-faint", c.textFaint}, {"accent", c.accent},
        {"accent-text", c.accentText}, {"danger", c.danger},        {"edge", c.edge},
        {"accent-edge", c.accentEdge},
    };
    QVariantList list;
    for (const auto &[name, color] : tokens)
        list.append(QVariantMap{{QStringLiteral("name"), QLatin1String(name)},
                                {QStringLiteral("color"), color}});
    return list;
}

int ThemeController::easingBounce() const
{
    return m_spec.motion.bounce > 0 ? QEasingCurve::OutBack : QEasingCurve::OutCubic;
}

} // namespace callie
