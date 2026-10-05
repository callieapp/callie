#include "ThemeController.h"

#include "callie/ThemeLoader.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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

QTEST_MAIN(TestThemeController)
#include "tst_themecontroller.moc"
