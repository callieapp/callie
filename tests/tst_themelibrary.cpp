#include "callie/ThemeLibrary.h"
#include "callie/ThemeLoader.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestThemeLibrary : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void copyStartsFromABuiltIn();
    void copiesNeverOverwrite();
    void copiesTakeAnyName();
    void setColorKeepsTheRest();
    void setColorAddsMissingKeys();
    void setColorRefusesOtherFiles();
    void importChecksTheTheme();
    void importMakesThemesFromBase16();
    void exportWritesABuiltIn();
    void removeOnlyOwnThemes();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<ThemeLibrary> m_library;

    QString read(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }
    QString writeFile(const QString &name, const QByteArray &data)
    {
        const QString path = m_dir->filePath(name);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
            file.write(data);
        return path;
    }
};

void TestThemeLibrary::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_library = std::make_unique<ThemeLibrary>(m_dir->filePath(u"themes"_s));
}

void TestThemeLibrary::copyStartsFromABuiltIn()
{
    const QString path = m_library->copy(u"callie"_s, u"Peach Fuzz"_s);
    QVERIFY2(!path.isEmpty(), qPrintable(m_library->error()));
    QVERIFY(path.endsWith(u"/peach-fuzz.toml"_s));
    QCOMPARE(m_library->themes(), QStringList{path});

    const ThemeLoadResult loaded = ThemeLoader::load(path);
    QVERIFY(loaded.ok());
    QCOMPARE(loaded.theme.name, u"Peach Fuzz"_s);
    QCOMPARE(loaded.theme.colors.accent, ThemeLoader::defaultTheme().colors.accent);
    QCOMPARE(ThemeLibrary::displayName(path), u"Peach Fuzz"_s);
    // Comments in the original survive the copy.
    QVERIFY(read(path).contains(u"# Callie, the default theme."_s));
}

void TestThemeLibrary::copiesTakeAnyName()
{
    // A quote, a backslash and a newline, as a pasted name may have.
    const QString path = m_library->copy(u"callie"_s, u"Bo \"Peep\" \\ Two\nLines"_s);
    QVERIFY2(!path.isEmpty(), qPrintable(m_library->error()));
    const ThemeLoadResult loaded = ThemeLoader::load(path);
    QVERIFY2(loaded.ok(), qPrintable(loaded.errors.join(u"; "_s)));
    QCOMPARE(loaded.theme.name, u"Bo \"Peep\" \\ TwoLines"_s);
}

void TestThemeLibrary::copiesNeverOverwrite()
{
    const QString first = m_library->copy(u"callie"_s, u"Mine"_s);
    const QString second = m_library->copy(u"callie"_s, u"Mine"_s);
    QVERIFY(first.endsWith(u"/mine.toml"_s));
    QVERIFY(second.endsWith(u"/mine-2.toml"_s));
}

void TestThemeLibrary::setColorKeepsTheRest()
{
    const QString path = m_library->copy(u"callie"_s, u"Mine"_s);
    const QString before = read(path);
    QVERIFY2(m_library->setColor(path, u"accent"_s, QColor(u"#ffaa00"_s)),
             qPrintable(m_library->error()));

    const ThemeLoadResult loaded = ThemeLoader::load(path);
    QCOMPARE(loaded.theme.colors.accent, QColor(u"#ffaa00"_s));
    QCOMPARE(loaded.theme.colors.surface, ThemeLoader::defaultTheme().colors.surface);
    // One line changed, nothing else.
    QCOMPARE(read(path).split(u'\n').size(), before.split(u'\n').size());
}

void TestThemeLibrary::setColorAddsMissingKeys()
{
    const QString path = m_dir->filePath(u"themes/small.toml"_s);
    QDir().mkpath(m_dir->filePath(u"themes"_s));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("[theme]\nname = \"Small\"\n");
    file.close();

    QVERIFY(m_library->setColor(path, u"accent"_s, QColor(u"#00aa88"_s)));
    QVERIFY(m_library->setColor(path, u"danger"_s, QColor(u"#aa0000"_s)));
    const ThemeLoadResult loaded = ThemeLoader::load(path);
    QVERIFY(loaded.ok());
    QCOMPARE(loaded.theme.colors.accent, QColor(u"#00aa88"_s));
    QCOMPARE(loaded.theme.colors.danger, QColor(u"#aa0000"_s));

    // Every key the editor offers is one the loader knows.
    for (const QString &key : ThemeLibrary::colorKeys())
        QVERIFY2(m_library->setColor(path, key, QColor(u"#336699"_s)), qPrintable(key));
    const ThemeLoadResult all = ThemeLoader::load(path);
    QVERIFY2(!all.warnings.join(u' ').contains(u"unknown"_s, Qt::CaseInsensitive),
             qPrintable(all.warnings.join(u"; "_s)));
    QCOMPARE(all.theme.colors.edge, QColor(u"#336699"_s));
    QVERIFY(m_library->setColor(path, u"accent"_s, QColor(u"#00aa88"_s)));

    QVERIFY(!m_library->setColor(path, u"no-such-color"_s, QColor(u"#000000"_s)));
    QCOMPARE(ThemeLoader::load(path).theme.colors.accent, QColor(u"#00aa88"_s));
}

void TestThemeLibrary::setColorRefusesOtherFiles()
{
    const QString outside = writeFile(u"outside.toml"_s, "[theme]\nname = \"Out\"\n");
    QVERIFY(!m_library->setColor(outside, u"accent"_s, QColor(u"#ffffff"_s)));
    QVERIFY(!read(outside).contains(u"accent"_s));
}

void TestThemeLibrary::importChecksTheTheme()
{
    const QString good = writeFile(u"good.toml"_s, "[theme]\nname = \"Good One\"\n"
                                                   "[colors]\naccent = \"#123456\"\n");
    const QString path = m_library->importTheme(good);
    QVERIFY2(!path.isEmpty(), qPrintable(m_library->error()));
    QVERIFY(path.endsWith(u"/good-one.toml"_s));

    // With no name of its own, the file name names it, not the default theme.
    const QString unnamed = writeFile(u"sunset.toml"_s, "[colors]\naccent = \"#ff8844\"\n");
    const QString unnamedPath = m_library->importTheme(unnamed);
    QVERIFY(unnamedPath.endsWith(u"/sunset.toml"_s));
    QCOMPARE(ThemeLibrary::displayName(unnamedPath), u"sunset"_s);
    QVERIFY(m_library->remove(unnamedPath));

    const QString bad = writeFile(u"bad.toml"_s, "[colors\naccent = \n");
    QVERIFY(m_library->importTheme(bad).isEmpty());
    QVERIFY(!m_library->error().isEmpty());
    QCOMPARE(m_library->themes().size(), 1);
}

void TestThemeLibrary::importMakesThemesFromBase16()
{
    // The classic layout, with bare hex.
    const QString night = writeFile(u"tomorrow-night.yaml"_s, R"yaml(scheme: "Tomorrow Night"
author: "Chris Kempson (http://chriskempson.com)"
base00: "1d1f21"
base01: "282a2e"
base02: "373b41"
base03: "969896"
base04: "b4b7b4"
base05: "c5c8c6"
base06: "e0e0e0"
base07: "ffffff"
base08: "cc6666"
base09: "de935f"
base0A: "f0c674"
base0B: "b5bd68"
base0C: "8abeb7"
base0D: "81a2be"
base0E: "b294bb"
base0F: "a3685a"
)yaml");
    const QString nightPath = m_library->importTheme(night);
    QVERIFY2(!nightPath.isEmpty(), qPrintable(m_library->error()));
    const ThemeLoadResult dark = ThemeLoader::loadFile(nightPath, ThemeLoader::defaultTheme());
    QVERIFY(dark.ok());
    QCOMPARE(dark.theme.name, u"Tomorrow Night"_s);
    QVERIFY(dark.theme.dark);
    QCOMPARE(dark.theme.colors.background, QColor(u"#1d1f21"_s));
    QCOMPARE(dark.theme.colors.accent, QColor(u"#b294bb"_s));
    QCOMPARE(ThemeLoader::contrastWarnings(dark.theme), QStringList());
    QVERIFY(read(nightPath).contains(u"by Chris Kempson"_s));

    // tinted-theming's layout, light, whose faint text is darkened to read.
    const QString solarized = writeFile(u"solarized-light.yml"_s, R"yaml(system: "base16"
name: 'Solarized "Light"'
author: "Ethan Schoonover"
variant: "light"
palette:
  base00: "#fdf6e3" # background
  base01: "#eee8d5"
  base02: "#93a1a1"
  base03: "#839496"
  base04: "#657b83"
  base05: "#586e75"
  base06: "#073642"
  base07: "#002b36"
  base08: "#dc322f"
  base09: "#cb4b16"
  base0A: "#b58900"
  base0B: "#859900"
  base0C: "#2aa198"
  base0D: "#268bd2"
  base0E: "#6c71c4"
  base0F: "#d33682"
)yaml");
    const QString lightPath = m_library->importTheme(solarized);
    QVERIFY2(!lightPath.isEmpty(), qPrintable(m_library->error()));
    const ThemeLoadResult light = ThemeLoader::loadFile(lightPath, ThemeLoader::defaultTheme());
    QVERIFY(light.ok());
    QCOMPARE(light.theme.name, u"Solarized \"Light\""_s);
    QVERIFY(!light.theme.dark);
    QCOMPARE(ThemeLoader::contrastWarnings(light.theme), QStringList());
    QVERIFY(light.theme.colors.textFaint != QColor(u"#839496"_s));

    const QString partial =
        writeFile(u"partial.yaml"_s, "scheme: \"Partial\"\nbase00: \"000000\"\n");
    QVERIFY(m_library->importTheme(partial).isEmpty());
    QVERIFY(m_library->error().contains(u"base01"_s));
    const QString wrong = writeFile(u"wrong.yaml"_s, "scheme: \"Wrong\"\nbase00: \"blue\"\n");
    QVERIFY(m_library->importTheme(wrong).isEmpty());
    QVERIFY(m_library->error().contains(u"not a color"_s));
    const QString bare = writeFile(u"bare.yaml"_s, "scheme: \"Bare\"\nbase00: #c5c8c\n");
    QVERIFY(m_library->importTheme(bare).isEmpty());
    QVERIFY(m_library->error().contains(u"#c5c8c"_s));
    QCOMPARE(m_library->themes().size(), 2);
}

void TestThemeLibrary::exportWritesABuiltIn()
{
    const QString destination = m_dir->filePath(u"out/callie.toml"_s);
    QDir().mkpath(m_dir->filePath(u"out"_s));
    QVERIFY2(m_library->exportTheme(u"callie"_s, destination), qPrintable(m_library->error()));
    QVERIFY(ThemeLoader::load(destination).ok());
}

void TestThemeLibrary::removeOnlyOwnThemes()
{
    const QString mine = m_library->copy(u"callie"_s, u"Mine"_s);
    const QString outside = writeFile(u"outside.toml"_s, "[theme]\n");
    QVERIFY(!m_library->remove(outside));
    QVERIFY(QFile::exists(outside));
    QVERIFY(m_library->remove(mine));
    QVERIFY(m_library->themes().isEmpty());
}

QTEST_GUILESS_MAIN(TestThemeLibrary)
#include "tst_themelibrary.moc"
