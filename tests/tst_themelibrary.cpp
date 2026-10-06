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
    void setColorKeepsTheRest();
    void setColorAddsMissingKeys();
    void setColorRefusesOtherFiles();
    void importChecksTheTheme();
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
