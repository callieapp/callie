#include "ThemeController.h"

#include "callie/Color.h"
#include "callie/Logging.h"
#include "callie/ThemeLoader.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

namespace callie {

ThemeController::ThemeController(QObject *parent)
    : QObject(parent), m_spec(ThemeLoader::defaultTheme()), m_watcher(new QFileSystemWatcher(this))
{
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

QVariantMap ThemeController::calendar() const
{
    return {{QStringLiteral("harmonize"), m_spec.calendar.harmonize},
            {QStringLiteral("lightness"), m_spec.calendar.lightness},
            {QStringLiteral("chroma"), m_spec.calendar.chroma}};
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
        {"accent-text", c.accentText}, {"danger", c.danger},
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
