#include "callie/ReleaseNotes.h"

#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestReleaseNotes : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void latestIsTheTopSection();
    void noReleaseYet();
    void bundledMatchesTheBuild();
};

void TestReleaseNotes::latestIsTheTopSection()
{
    const auto notes = ReleaseNotes::latest(R"(# Changelog

Intro.

## 0.2.0 - 2026-11-01

Cozier.

### New

- Things.

## 0.1.0 - 2026-10-06

First.
)");
    QVERIFY(notes);
    QCOMPARE(notes->version, u"0.2.0"_s);
    QCOMPARE(notes->date, QDate(2026, 11, 1));
    QCOMPARE(notes->body, u"Cozier.\n\n### New\n\n- Things."_s);
}

void TestReleaseNotes::noReleaseYet()
{
    QVERIFY(!ReleaseNotes::latest("# Changelog\n\n## Unreleased\n\n- Soon.\n"));
    QVERIFY(!ReleaseNotes::latest(""));
}

void TestReleaseNotes::bundledMatchesTheBuild()
{
    const auto notes = ReleaseNotes::latest(ReleaseNotes::bundledChangelog());
    QVERIFY(notes);
    QCOMPARE(notes->version, QStringLiteral(CALLIE_VERSION));
    QVERIFY(!notes->body.isEmpty());
}

QTEST_GUILESS_MAIN(TestReleaseNotes)
#include "tst_releasenotes.moc"
