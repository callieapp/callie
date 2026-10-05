#include "callie/ThemeLoader.h"

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
    void partialThemeInheritsDefault();
    void integerAcceptedForNumber();
    void invalidColorIsAnError();
    void wrongTypeIsAnError();
    void outOfRangeIsAnError();
    void unknownKeyIsAWarning();
    void syntaxErrorReportsLine();
    void lowContrastIsAWarning();
    void unknownBuiltInIsAnError();
    void incompleteBaseIsAnError();
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

QTEST_GUILESS_MAIN(TestTheme)
#include "tst_theme.moc"
