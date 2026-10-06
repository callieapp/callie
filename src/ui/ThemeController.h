#pragma once

#include "callie/ThemeSpec.h"

#include <QColor>
#include <QEasingCurve>
#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantList>

class QFileSystemWatcher;

namespace callie {

/// The `Theme` singleton QML reads every visual value from. Theme-driven values
/// come from the active theme file and change together; spacing, type sizes and
/// grid metrics are constants, because they are product decisions.
class ThemeController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Theme)
    QML_SINGLETON

    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY changed)
    /// Every color token as `{name, color}`, for the gallery.
    Q_PROPERTY(QVariantList swatches READ swatches NOTIFY changed)
    /// `{harmonize, lightness, chroma}`; pass it to calendarColor().
    Q_PROPERTY(QVariantMap calendar READ calendar NOTIFY changed)

    Q_PROPERTY(QColor bg READ bg NOTIFY changed)
    Q_PROPERTY(QColor surface READ surface NOTIFY changed)
    Q_PROPERTY(QColor surfaceAlt READ surfaceAlt NOTIFY changed)
    Q_PROPERTY(QColor hairline READ hairline NOTIFY changed)
    Q_PROPERTY(QColor border READ border NOTIFY changed)
    Q_PROPERTY(QColor text READ text NOTIFY changed)
    Q_PROPERTY(QColor textMuted READ textMuted NOTIFY changed)
    Q_PROPERTY(QColor textFaint READ textFaint NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor accentText READ accentText NOTIFY changed)
    Q_PROPERTY(QColor danger READ danger NOTIFY changed)
    Q_PROPERTY(QColor edge READ edge NOTIFY changed)
    Q_PROPERTY(QColor accentEdge READ accentEdge NOTIFY changed)

    Q_PROPERTY(int radiusSm READ radiusSm NOTIFY changed)
    Q_PROPERTY(int radiusMd READ radiusMd NOTIFY changed)
    Q_PROPERTY(int radiusLg READ radiusLg NOTIFY changed)
    Q_PROPERTY(int radiusXl READ radiusXl NOTIFY changed)
    Q_PROPERTY(int stickerEdge READ stickerEdge NOTIFY changed)

    /// The shadow under menus, popovers and tooltips; `shadowColor` carries its opacity.
    Q_PROPERTY(QColor shadowColor READ shadowColor NOTIFY changed)
    Q_PROPERTY(int shadowBlur READ shadowBlur NOTIFY changed)
    Q_PROPERTY(int shadowOffset READ shadowOffset NOTIFY changed)

    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY changed)
    Q_PROPERTY(QString displayFontFamily READ displayFontFamily NOTIFY changed)
    Q_PROPERTY(QString monoFontFamily READ monoFontFamily NOTIFY changed)

    Q_PROPERTY(int durFast READ durFast NOTIFY changed)
    Q_PROPERTY(int durMed READ durMed NOTIFY changed)
    Q_PROPERTY(int durSlow READ durSlow NOTIFY changed)
    /// For color and opacity changes, which should never overshoot.
    Q_PROPERTY(int easing READ easing CONSTANT)
    /// For things arriving or settling, with `bounce` as the overshoot.
    Q_PROPERTY(int easingBounce READ easingBounce NOTIFY changed)
    Q_PROPERTY(qreal bounce READ bounce NOTIFY changed)

    Q_PROPERTY(int space1 READ space1 CONSTANT)
    Q_PROPERTY(int space2 READ space2 CONSTANT)
    Q_PROPERTY(int space3 READ space3 CONSTANT)
    Q_PROPERTY(int space4 READ space4 CONSTANT)
    Q_PROPERTY(int space5 READ space5 CONSTANT)
    Q_PROPERTY(int space6 READ space6 CONSTANT)
    Q_PROPERTY(int space7 READ space7 CONSTANT)

    Q_PROPERTY(int textXs READ textXs CONSTANT)
    Q_PROPERTY(int textSm READ textSm CONSTANT)
    Q_PROPERTY(int textMd READ textMd CONSTANT)
    Q_PROPERTY(int textBase READ textBase CONSTANT)
    Q_PROPERTY(int textLg READ textLg CONSTANT)
    Q_PROPERTY(int textDate READ textDate CONSTANT)
    Q_PROPERTY(int textXl READ textXl CONSTANT)
    Q_PROPERTY(int textDisplay READ textDisplay CONSTANT)
    Q_PROPERTY(int text2xl READ text2xl CONSTANT)

    /// Washes over day columns: a hint of pink on today, a shade on days off.
    Q_PROPERTY(QColor todayWash READ todayWash NOTIFY changed)
    Q_PROPERTY(QColor dayOffWash READ dayOffWash NOTIFY changed)

    Q_PROPERTY(int hourHeight READ hourHeight CONSTANT)
    Q_PROPERTY(int gutterWidth READ gutterWidth CONSTANT)
    Q_PROPERTY(int allDayRowHeight READ allDayRowHeight CONSTANT)
    Q_PROPERTY(int snapMinutes READ snapMinutes CONSTANT)
    /// The grab strip along a frameless window's edges. Anything scrollable at
    /// an edge keeps its handle clear of it.
    Q_PROPERTY(int resizeBorder READ resizeBorder CONSTANT)

public:
    /// The process-wide instance, which QML also receives.
    static ThemeController *instance();
    static ThemeController *create(QQmlEngine *, QJSEngine *);

    /// Applies an installed theme by id, or a theme file by path, and watches a
    /// file for edits. On failure the current theme stays and errors are returned.
    QStringList load(const QString &idOrPath);

    [[nodiscard]] const ThemeSpec &spec() const { return m_spec; }

    Q_INVOKABLE QColor tint(const QColor &color, qreal alpha) const;
    /// A calendar's own color fitted to a theme. Call it as
    /// `Theme.calendarColor(color, Theme.calendar)`: QML cannot see what a C++
    /// call reads, so passing the settings is what makes the binding update.
    Q_INVOKABLE QColor calendarColor(const QColor &source, const QVariantMap &calendar) const;
    /// The ink drawn on calendarColor() and the edge under it, called the same way.
    Q_INVOKABLE QColor calendarInk(const QColor &source, const QVariantMap &calendar) const;
    Q_INVOKABLE QColor calendarEdge(const QColor &source, const QVariantMap &calendar) const;
    Q_INVOKABLE double contrast(const QColor &a, const QColor &b) const;

    QVariantList swatches() const;
    QVariantMap calendar() const;

    QString name() const { return m_spec.name; }
    bool dark() const { return m_spec.dark; }
    QStringList warnings() const { return m_warnings; }

    QColor bg() const { return m_spec.colors.background; }
    QColor surface() const { return m_spec.colors.surface; }
    QColor surfaceAlt() const { return m_spec.colors.surfaceAlt; }
    QColor hairline() const { return m_spec.colors.hairline; }
    QColor border() const { return m_spec.colors.border; }
    QColor text() const { return m_spec.colors.text; }
    QColor textMuted() const { return m_spec.colors.textMuted; }
    QColor textFaint() const { return m_spec.colors.textFaint; }
    QColor accent() const { return m_spec.colors.accent; }
    QColor accentText() const { return m_spec.colors.accentText; }
    QColor danger() const { return m_spec.colors.danger; }
    QColor edge() const { return m_spec.colors.edge; }
    QColor accentEdge() const { return m_spec.colors.accentEdge; }

    int radiusSm() const { return m_spec.shape.radiusSmall; }
    int radiusMd() const { return m_spec.shape.radius; }
    int radiusLg() const { return m_spec.shape.radiusLarge; }
    int radiusXl() const { return m_spec.shape.radiusXLarge; }
    int stickerEdge() const { return m_spec.shape.stickerEdge; }

    QColor shadowColor() const;
    int shadowBlur() const { return m_spec.shadow.blur; }
    int shadowOffset() const { return m_spec.shadow.offset; }

    QString fontFamily() const { return m_spec.type.family; }
    QString displayFontFamily() const { return m_spec.type.displayFamily; }
    QString monoFontFamily() const { return m_spec.type.monoFamily; }

    int durFast() const { return m_spec.motion.durationFast; }
    int durMed() const { return m_spec.motion.duration; }
    int durSlow() const { return m_spec.motion.durationSlow; }
    int easing() const { return QEasingCurve::OutCubic; }
    int easingBounce() const;
    qreal bounce() const { return m_spec.motion.bounce; }

    int space1() const { return 2; }
    int space2() const { return 4; }
    int space3() const { return 8; }
    int space4() const { return 12; }
    int space5() const { return 16; }
    int space6() const { return 24; }
    int space7() const { return 32; }

    int textXs() const { return 11; }
    int textSm() const { return 12; }
    int textMd() const { return 13; }
    int textBase() const { return 14; }
    int textLg() const { return 15; }
    int textDate() const { return 18; }
    int textXl() const { return 20; }
    int textDisplay() const { return 22; }
    int text2xl() const { return 28; }

    QColor todayWash() const;
    QColor dayOffWash() const;

    int hourHeight() const { return 56; }
    int gutterWidth() const { return 64; }
    int allDayRowHeight() const { return 22; }
    int snapMinutes() const { return 15; }
    int resizeBorder() const { return 6; }

Q_SIGNALS:
    void changed();

private:
    explicit ThemeController(QObject *parent = nullptr);
    void reloadWatchedFile();

    ThemeSpec m_spec;
    QStringList m_warnings;
    QString m_watchedPath;
    QFileSystemWatcher *m_watcher;
};

} // namespace callie
