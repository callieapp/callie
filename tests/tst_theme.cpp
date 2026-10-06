#include "callie/ThemeLoader.h"

#include <QColor>
#include <QTest>

using namespace callie;

namespace {

ThemeLoadResult parse(const char *toml)
{
    return ThemeLoader::parse(QByteArrayView(toml), ThemeLoader::defaultTheme());
}

bool anyContains(const QStringList &messages, const char *needle)
{
    for (const QString &message : messages) {
        if (message.contains(QLatin1String(needle)))
            return true;
    }
    return false;
}

} // namespace

class TestTheme : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void defaultThemeIsCompleteAndClean();
    void builtInIdsIncludeDefault();
    void lightThemeIsClean();
    void partialThemeInheritsDefault();
    void integerAcceptedForNumber();
    void invalidColorIsAnError();
    void wrongTypeIsAnError();
    void outOfRangeIsAnError();
    void nanIsAnError();
    void unknownKeyIsAWarning();
    void syntaxErrorReportsLine();
    void lowContrastIsAWarning();
    void unknownBuiltInIsAnError();
    void incompleteBaseIsAnError();
    void stickerAndShadowSettingsAreRead();
    void lowInkContrastIsAWarning();
    void shadowOpacityIsBounded();
};

void TestTheme::defaultThemeIsCompleteAndClean()
{
    const ThemeLoadResult result = ThemeLoader::loadBuiltIn(QStringLiteral("callie"));
    QVERIFY2(result.ok(), qPrintable(result.errors.join(u'\n')));
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join(u'\n')));
    QCOMPARE(result.theme.name, QStringLiteral("Callie"));
    QVERIFY(result.theme.dark);
}

void TestTheme::builtInIdsIncludeDefault()
{
    QVERIFY(ThemeLoader::builtInIds().contains(QStringLiteral("callie")));
}

void TestTheme::lightThemeIsClean()
{
    QVERIFY(ThemeLoader::builtInIds().contains(QStringLiteral("callie-light")));
    const ThemeLoadResult light = ThemeLoader::loadBuiltIn(QStringLiteral("callie-light"));
    QVERIFY2(light.ok(), qPrintable(light.errors.join(u'\n')));
    QVERIFY2(light.warnings.isEmpty(), qPrintable(light.warnings.join(u'\n')));
    QCOMPARE(light.theme.name, QStringLiteral("Callie Light"));
    QVERIFY(!light.theme.dark);
    QVERIFY(light.theme.colors.background.lightness() > 200);
    // Everything not set keeps the default's identity.
    QCOMPARE(light.theme.type.displayFamily, ThemeLoader::defaultTheme().type.displayFamily);
}

void TestTheme::partialThemeInheritsDefault()
{
    const ThemeLoadResult result =
        parse("[theme]\nname = \"Blue\"\n[colors]\naccent = \"#5b8def\"\n");
    QVERIFY(result.ok());
    QCOMPARE(result.theme.name, QStringLiteral("Blue"));
    QCOMPARE(result.theme.colors.accent, QColor("#5b8def"));
    QCOMPARE(result.theme.colors.background, ThemeLoader::defaultTheme().colors.background);
    QCOMPARE(result.theme.shape.radius, ThemeLoader::defaultTheme().shape.radius);
}

void TestTheme::integerAcceptedForNumber()
{
    const ThemeLoadResult result = parse("[calendar]\nlightness = 1\n");
    QVERIFY(result.ok());
    QCOMPARE(result.theme.calendar.lightness, 1.0);
}

void TestTheme::invalidColorIsAnError()
{
    const ThemeLoadResult result = parse("[colors]\naccent = \"pink\"\n");
    QVERIFY(!result.ok());
    QVERIFY(anyContains(result.errors, "colors.accent"));
}

void TestTheme::wrongTypeIsAnError()
{
    const ThemeLoadResult result = parse("[shape]\nradius = \"4\"\n");
    QVERIFY(!result.ok());
    QVERIFY(anyContains(result.errors, "shape.radius"));
}

void TestTheme::outOfRangeIsAnError()
{
    const ThemeLoadResult result = parse("[calendar]\nlightness = 2.0\n");
    QVERIFY(!result.ok());
    QVERIFY(anyContains(result.errors, "calendar.lightness"));
}

void TestTheme::nanIsAnError()
{
    const ThemeLoadResult result = parse("[calendar]\nchroma = nan\n");
    QVERIFY(!result.ok());
    QVERIFY(anyContains(result.errors, "calendar.chroma"));
}

void TestTheme::unknownKeyIsAWarning()
{
    const ThemeLoadResult result = parse("[colors]\nbackgroud = \"#000000\"\n");
    QVERIFY(result.ok());
    QVERIFY(anyContains(result.warnings, "colors.backgroud"));
}

void TestTheme::syntaxErrorReportsLine()
{
    const ThemeLoadResult result = parse("[colors]\naccent = \n");
    QVERIFY(!result.ok());
    QVERIFY2(result.errors.first().startsWith(QStringLiteral("line 2")),
             qPrintable(result.errors.first()));
}

void TestTheme::lowContrastIsAWarning()
{
    const ThemeLoadResult result = parse("[colors]\ntext = \"#2a2028\"\n");
    QVERIFY(result.ok());
    QVERIFY(anyContains(result.warnings, "text on background"));
}

void TestTheme::unknownBuiltInIsAnError()
{
    QVERIFY(!ThemeLoader::loadBuiltIn(QStringLiteral("no-such-theme")).ok());
}

void TestTheme::incompleteBaseIsAnError()
{
    // The default must set every color, because every other theme falls back to it.
    const ThemeLoadResult result = ThemeLoader::parse(QByteArrayView("[colors]\n"), ThemeSpec{});
    QVERIFY(!result.ok());
    QVERIFY(anyContains(result.errors, "colors.background: missing"));
}

void TestTheme::stickerAndShadowSettingsAreRead()
{
    const ThemeLoadResult result = parse("[colors]\nedge = \"#010203\"\naccent-edge = \"#040506\"\n"
                                         "[calendar]\nink-lightness = 0.2\nedge-chroma = 0.1\n"
                                         "[shape]\nsticker-edge = 0\n"
                                         "[shadow]\nopacity = 0.3\nblur = 20\noffset = 6\n");
    QVERIFY2(result.ok(), qPrintable(result.errors.join(u'\n')));
    QCOMPARE(result.theme.colors.edge, QColor(1, 2, 3));
    QCOMPARE(result.theme.colors.accentEdge, QColor(4, 5, 6));
    QCOMPARE(result.theme.calendar.inkLightness, 0.2);
    QCOMPARE(result.theme.calendar.edgeChroma, 0.1);
    QCOMPARE(result.theme.shape.stickerEdge, 0);
    QCOMPARE(result.theme.shadow.opacity, 0.3);
    QCOMPARE(result.theme.shadow.blur, 20);
    QCOMPARE(result.theme.shadow.offset, 6);
    // Unset keys keep the default theme's values.
    QCOMPARE(result.theme.calendar.edgeLightness,
             ThemeLoader::defaultTheme().calendar.edgeLightness);
}

void TestTheme::lowInkContrastIsAWarning()
{
    // Ink nearly as light as the fill it sits on.
    const ThemeLoadResult result = parse("[calendar]\nink-lightness = 0.75\n");
    QVERIFY(result.ok());
    QVERIFY(anyContains(result.warnings, "calendar ink on its fill"));
}

void TestTheme::shadowOpacityIsBounded()
{
    const ThemeLoadResult result = parse("[shadow]\nopacity = 1.5\n");
    QVERIFY(!result.ok());
    QVERIFY(anyContains(result.errors, "shadow.opacity"));
}

QTEST_GUILESS_MAIN(TestTheme)
#include "tst_theme.moc"
