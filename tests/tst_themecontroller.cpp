#include "ThemeController.h"

#include "callie/Color.h"
#include "callie/ThemeLoader.h"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace callie;

namespace {

void writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(contents);
}

} // namespace

class TestThemeController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void startsWithDefaultTheme();
    void loadsAFileAndReloadsOnEdit();
    void brokenEditKeepsLastGoodTheme();
    void calendarColorRespectsHarmonizeSetting();
    void builtInIdIgnoresSameNamedFile();
    void stickerColorsShareTheCalendarHue();
    void unfittedInkPicksTheReadableOne();
    void armedDangerReadsInBothThemes();
    void bundledFontsAreAvailable();
};

void TestThemeController::startsWithDefaultTheme()
{
    QCOMPARE(ThemeController::instance()->name(), ThemeLoader::defaultTheme().name);
}

void TestThemeController::loadsAFileAndReloadsOnEdit()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("mine.toml"));
    writeFile(path, "[theme]\nname = \"Mine\"\n[colors]\naccent = \"#5b8def\"\n");

    ThemeController *theme = ThemeController::instance();
    QVERIFY(theme->load(path).isEmpty());
    QCOMPARE(theme->name(), QStringLiteral("Mine"));
    QCOMPARE(theme->accent(), QColor("#5b8def"));

    QSignalSpy changed(theme, &ThemeController::changed);
    writeFile(path, "[theme]\nname = \"Mine\"\n[colors]\naccent = \"#2fa98c\"\n");
    QVERIFY(changed.wait(3000));
    QCOMPARE(theme->accent(), QColor("#2fa98c"));
}

void TestThemeController::brokenEditKeepsLastGoodTheme()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("mine.toml"));
    writeFile(path, "[colors]\naccent = \"#5b8def\"\n");
    ThemeController *theme = ThemeController::instance();
    QVERIFY(theme->load(path).isEmpty());

    QSignalSpy changed(theme, &ThemeController::changed);
    writeFile(path, "[colors]\naccent = \n");
    QTest::qWait(500);
    QCOMPARE(changed.size(), 0);
    QCOMPARE(theme->accent(), QColor("#5b8def"));
}

void TestThemeController::calendarColorRespectsHarmonizeSetting()
{
    ThemeController *theme = ThemeController::instance();
    const QColor source("#039be5");
    QVariantMap settings = theme->calendar();
    QVERIFY(theme->calendarColor(source, settings) != source);

    settings[QStringLiteral("harmonize")] = false;
    QCOMPARE(theme->calendarColor(source, settings), source);
}

void TestThemeController::builtInIdIgnoresSameNamedFile()
{
    // A file called "callie" in the working directory must not be mistaken for
    // the built-in theme of that name and watched in its place.
    QTemporaryDir dir;
    const QString previous = QDir::currentPath();
    QDir::setCurrent(dir.path());
    const QString impostor = dir.filePath(QStringLiteral("callie"));
    writeFile(impostor, "not a theme");

    ThemeController *theme = ThemeController::instance();
    QVERIFY(theme->load(QStringLiteral("callie")).isEmpty());
    QSignalSpy changed(theme, &ThemeController::changed);
    writeFile(impostor, "[theme]\nname = \"Impostor\"\n");
    QTest::qWait(500);
    QCOMPARE(changed.size(), 0);
    QCOMPARE(theme->name(), QStringLiteral("Callie"));

    QDir::setCurrent(previous);
}

void TestThemeController::stickerColorsShareTheCalendarHue()
{
    ThemeController *theme = ThemeController::instance();
    const QColor source(QStringLiteral("#5B8DEF"));
    const QVariantMap calendar = theme->calendar();
    const color::Oklch fill = color::toOklch(theme->calendarColor(source, calendar));
    const color::Oklch ink = color::toOklch(theme->calendarInk(source, calendar));
    const color::Oklch edge = color::toOklch(theme->calendarEdge(source, calendar));

    QVERIFY(ink.lightness < edge.lightness);
    QVERIFY(edge.lightness < fill.lightness);
    QVERIFY(std::abs(ink.hue - fill.hue) < 6);
    QVERIFY(std::abs(edge.hue - fill.hue) < 6);
    QVERIFY(color::contrastRatio(theme->calendarInk(source, calendar),
                                 theme->calendarColor(source, calendar)) >= 4.5);
}

void TestThemeController::unfittedInkPicksTheReadableOne()
{
    ThemeController *theme = ThemeController::instance();
    QVariantMap calendar = theme->calendar();
    calendar.insert(QStringLiteral("harmonize"), false);

    QCOMPARE(theme->calendarInk(QColor(QStringLiteral("#ffe680")), calendar), QColor(0, 0, 0));
    QCOMPARE(theme->calendarInk(QColor(QStringLiteral("#202060")), calendar),
             QColor(255, 255, 255));
    QVERIFY(theme->calendarEdge(QColor(QStringLiteral("#ffe680")), calendar).lightness() <
            QColor(QStringLiteral("#ffe680")).lightness());
}

void TestThemeController::armedDangerReadsInBothThemes()
{
    ThemeController *theme = ThemeController::instance();
    for (const QString &id : {QStringLiteral("callie"), QStringLiteral("callie-light")}) {
        QVERIFY(theme->load(id).isEmpty());
        // A filled red button's label reads as body text does.
        QVERIFY2(color::contrastRatio(theme->dangerText(), theme->danger()) >= 4.5, qPrintable(id));
        QCOMPARE(theme->dangerText() == theme->text() || theme->dangerText() == theme->bg(), true);
    }
    QVERIFY(theme->load(QStringLiteral("callie")).isEmpty());
}

void TestThemeController::bundledFontsAreAvailable()
{
    ThemeController::instance();
    const QStringList families = QFontDatabase::families();
    QVERIFY(families.contains(QStringLiteral("Nunito")));
    QVERIFY(families.contains(QStringLiteral("Fraunces")));
}

QTEST_MAIN(TestThemeController)
#include "tst_themecontroller.moc"
