#include "callie/ViewRange.h"

#include <QTest>

using namespace callie;
using namespace callie::ViewRange;
using namespace Qt::StringLiterals;

class TestViewRange : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void namesFallBackToWeek();
    void rangesAroundAWednesday();
    void monthsAreWholeWeeks();
    void weeksCanStartOnAnyDay();
    void stepsMoveByTheView();
    void headingsNameNearbyDays();
};

void TestViewRange::namesFallBackToWeek()
{
    QCOMPARE(fromName(u"month"_s), View::Month);
    QCOMPARE(fromName(u"agenda"_s), View::Agenda);
    QCOMPARE(fromName(u"year"_s), View::Week);
}

void TestViewRange::rangesAroundAWednesday()
{
    const QDate wednesday(2026, 10, 7);
    QCOMPARE(start(View::Day, wednesday), wednesday);
    QCOMPARE(start(View::Week, wednesday), QDate(2026, 10, 5));
    QCOMPARE(start(View::Agenda, wednesday), wednesday);
    QCOMPARE(days(View::Day, wednesday), 1);
    QCOMPARE(days(View::Week, wednesday), 7);
    QCOMPARE(days(View::Agenda, wednesday), kAgendaDays);
}

void TestViewRange::weeksCanStartOnAnyDay()
{
    const QDate wednesday(2026, 10, 7);
    QCOMPARE(start(View::Week, wednesday, Qt::Sunday), QDate(2026, 10, 4));
    QCOMPARE(start(View::Week, wednesday, Qt::Saturday), QDate(2026, 10, 3));
    QCOMPARE(start(View::Week, wednesday, Qt::Wednesday), wednesday);
    // October 2026 starts on a Thursday: from Sunday, five whole weeks cover it.
    QCOMPARE(start(View::Month, wednesday, Qt::Sunday), QDate(2026, 9, 27));
    QCOMPARE(days(View::Month, wednesday, Qt::Sunday), 35);
    // November 2026 starts on a Sunday and needs five weeks from Sunday, six from Monday.
    QCOMPARE(days(View::Month, QDate(2026, 11, 10), Qt::Sunday), 35);
    QCOMPARE(days(View::Month, QDate(2026, 11, 10), Qt::Monday), 42);
    // A Sunday-first week takes the number most of its days have.
    QCOMPARE(weekNumber(QDate(2026, 10, 4), Qt::Sunday), 41);
    QCOMPARE(weekNumber(QDate(2026, 10, 4), Qt::Monday), 40);
}

void TestViewRange::monthsAreWholeWeeks()
{
    // October 2026 starts on a Thursday: from Monday 28 September, five weeks.
    QCOMPARE(start(View::Month, QDate(2026, 10, 20)), QDate(2026, 9, 28));
    QCOMPARE(days(View::Month, QDate(2026, 10, 20)), 35);
    // February 2027 starts on a Monday and fits four weeks exactly.
    QCOMPARE(start(View::Month, QDate(2027, 2, 14)), QDate(2027, 2, 1));
    QCOMPARE(days(View::Month, QDate(2027, 2, 14)), 28);
    // August 2026 starts on a Saturday and needs six.
    QCOMPARE(days(View::Month, QDate(2026, 8, 1)), 42);
}

void TestViewRange::stepsMoveByTheView()
{
    const QDate day(2026, 1, 31);
    QCOMPARE(step(View::Day, day, -1), QDate(2026, 1, 30));
    QCOMPARE(step(View::Week, day, 1), QDate(2026, 2, 7));
    QCOMPARE(step(View::Agenda, day, 1), day.addDays(kAgendaDays));
    // From the 31st, a month on is still February.
    QCOMPARE(step(View::Month, day, 1), QDate(2026, 2, 1));
    QCOMPARE(step(View::Month, day, -2), QDate(2025, 11, 1));
}

void TestViewRange::headingsNameNearbyDays()
{
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    const QDate today(2026, 10, 7);
    QCOMPARE(heading(today, today), u"Today"_s);
    QCOMPARE(heading(today.addDays(1), today), u"Tomorrow"_s);
    QCOMPARE(heading(today.addDays(-1), today), u"Yesterday"_s);
    QCOMPARE(heading(QDate(2026, 10, 9), today), u"Friday, October 9"_s);
    QLocale::setDefault(QLocale::c());
}

QTEST_GUILESS_MAIN(TestViewRange)
#include "tst_viewrange.moc"
