#include "EventModelForeign.h"
#include "ThemeController.h"
#include "ThemesController.h"

#include "callie/Settings.h"
#include "callie/ThemeLoader.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestThemes : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void customizeMakesAnEditableCopy();
    void builtInsCannotBeEdited();
    void removeCurrentGoesBackToDefault();
    void restoreForgetsAThemeThatNoLongerLoads();

private:
    QTemporaryDir m_dir;
    std::unique_ptr<Settings> m_settings;
    ThemesController *themes() { return ThemesController::instance(); }
};

void TestThemes::initTestCase()
{
    // The user's themes folder and settings are never touched.
    qputenv("XDG_CONFIG_HOME", m_dir.filePath(u"config"_s).toUtf8());
    m_settings = std::make_unique<Settings>(m_dir.filePath(u"settings.ini"_s));
    SettingsForeign::s_instance = m_settings.get();
}

void TestThemes::init()
{
    themes()->useDefault();
}

void TestThemes::customizeMakesAnEditableCopy()
{
    QVERIFY(!themes()->editable());
    QVERIFY(themes()->customize());

    QVERIFY(themes()->editable());
    const QString path = themes()->current();
    QVERIFY(path.startsWith(m_dir.filePath(u"config/callie/themes"_s)));
    QCOMPARE(m_settings->theme(), path);

    QVERIFY2(themes()->setColor(u"accent"_s, QColor(u"#33cc99"_s)), qPrintable(themes()->error()));
    QCOMPARE(ThemeController::instance()->spec().colors.accent, QColor(u"#33cc99"_s));
    QCOMPARE(themes()->colors().value(u"accent"_s).value<QColor>(), QColor(u"#33cc99"_s));
    QVERIFY(themes()->removeCurrent());
}

void TestThemes::builtInsCannotBeEdited()
{
    QVERIFY(!themes()->setColor(u"accent"_s, QColor(u"#000000"_s)));
    QVERIFY(!themes()->error().isEmpty());
    QCOMPARE(ThemeController::instance()->spec().colors.accent,
             ThemeLoader::defaultTheme().colors.accent);
}

void TestThemes::removeCurrentGoesBackToDefault()
{
    QVERIFY(themes()->customize());
    const QString path = themes()->current();

    QVERIFY(themes()->removeCurrent());

    QVERIFY(!QFile::exists(path));
    QCOMPARE(themes()->current(), u"callie"_s);
    QVERIFY(m_settings->theme().isEmpty());
    QVERIFY(!themes()->editable());
}

void TestThemes::restoreForgetsAThemeThatNoLongerLoads()
{
    m_settings->setTheme(m_dir.filePath(u"gone.toml"_s));
    themes()->restore();
    QVERIFY(m_settings->theme().isEmpty());
    QCOMPARE(ThemeController::instance()->spec().name, ThemeLoader::defaultTheme().name);
}

QTEST_MAIN(TestThemes)
#include "tst_themes.moc"
