#include "callie/Repeat.h"

#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestRepeat : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void customReadsWhatTheFormCanSay();
    void customLeavesOtherRulesAlone();
    void customWritesItsRule();
    void customInWords();
    void choicesBecomeRules();
    void rulesAreReadBack();
    void choicesInWords();
};

void TestRepeat::choicesBecomeRules()
{
    const QDate tuesday(2026, 10, 13); // the second Tuesday of October
    QVERIFY(Repeat::rule(u"none"_s, tuesday).isEmpty());
    QCOMPARE(Repeat::rule(u"weekly"_s, tuesday), QStringList{u"RRULE:FREQ=WEEKLY;BYDAY=TU"_s});
    QCOMPARE(Repeat::rule(u"monthly"_s, tuesday),
             QStringList{u"RRULE:FREQ=MONTHLY;BYMONTHDAY=13"_s});
    QCOMPARE(Repeat::rule(u"monthlyWeekday"_s, tuesday),
             QStringList{u"RRULE:FREQ=MONTHLY;BYDAY=2TU"_s});
    // The last Tuesday is the last, whichever number it is.
    QCOMPARE(Repeat::rule(u"monthlyWeekday"_s, QDate(2026, 10, 27)),
             QStringList{u"RRULE:FREQ=MONTHLY;BYDAY=-1TU"_s});
    QCOMPARE(Repeat::rule(u"weekdays"_s, tuesday),
             QStringList{u"RRULE:FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR"_s});
}

void TestRepeat::rulesAreReadBack()
{
    const QDate tuesday(2026, 10, 13);
    for (const QString &choice : Repeat::choices())
        QCOMPARE(Repeat::choiceOf(Repeat::rule(choice, tuesday), tuesday), choice);
    QCOMPARE(Repeat::choiceOf({u"RRULE:FREQ=WEEKLY"_s}, tuesday), u"weekly"_s);
    QCOMPARE(Repeat::choiceOf({u"RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=TU"_s}, tuesday), u"custom"_s);
    // A rule read from another day is not that day's choice.
    QCOMPARE(Repeat::choiceOf(Repeat::rule(u"weekly"_s, tuesday), tuesday.addDays(1)), u"custom"_s);
}

void TestRepeat::choicesInWords()
{
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    const QDate tuesday(2026, 10, 13);
    QCOMPARE(Repeat::describe(u"weekly"_s, tuesday), u"Weekly on Tuesday"_s);
    QCOMPARE(Repeat::describe(u"monthlyWeekday"_s, tuesday), u"Monthly on the second Tuesday"_s);
    QCOMPARE(Repeat::describe(u"monthlyWeekday"_s, QDate(2026, 10, 27)),
             u"Monthly on the last Tuesday"_s);
    QCOMPARE(Repeat::describe(u"yearly"_s, tuesday), u"Yearly on October 13"_s);
}

void TestRepeat::customReadsWhatTheFormCanSay()
{
    // Wednesday 7 October 2026, the first Wednesday of the month.
    const QDate start(2026, 10, 7);
    const QTimeZone york("America/New_York");
    const auto read = [&](const char *line) {
        return Repeat::custom({QString::fromLatin1(line)}, start, york);
    };
    Repeat::Custom weekly;
    weekly.interval = 2;
    weekly.weekdays = {1, 3};
    QCOMPARE(*read("RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=WE,MO"), weekly);

    Repeat::Custom monthly;
    monthly.frequency = u"monthly"_s;
    monthly.onWeekday = true;
    monthly.count = 6;
    QCOMPARE(*read("RRULE:FREQ=MONTHLY;BYDAY=1WE;COUNT=6"), monthly);

    // A UTC until is the day it falls on in the event's zone.
    const auto daily = read("RRULE:FREQ=DAILY;UNTIL=20261201T035959Z");
    QVERIFY(daily);
    QCOMPARE(daily->frequency, u"daily"_s);
    QCOMPARE(daily->until, QDate(2026, 11, 30));
    QCOMPARE(read("RRULE:FREQ=YEARLY;UNTIL=20301007")->until, QDate(2030, 10, 7));
    // Weekly with no days is on the start's own.
    QCOMPARE(read("RRULE:FREQ=WEEKLY")->weekdays, QList<int>{3});
    // Lines other than the rule, such as deleted days, do not stop it.
    QVERIFY(Repeat::custom({u"RRULE:FREQ=DAILY"_s, u"EXDATE:20261009T133000Z"_s}, start, york));
    // The start's own month and day restate a yearly rule.
    QCOMPARE(read("RRULE:FREQ=YEARLY;BYMONTH=10;BYMONTHDAY=7")->frequency, u"yearly"_s);
    // A week starting on Sunday is kept, since it moves which weeks count.
    const auto sundays = read("RRULE:FREQ=WEEKLY;INTERVAL=2;WKST=SU;BYDAY=SU,MO");
    QVERIFY(sundays);
    QCOMPARE(sundays->weekStart, u"SU"_s);
    QCOMPARE(Repeat::rule(*sundays, start, false, york),
             QStringList{u"RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,SU;WKST=SU"_s});
    QVERIFY(read("RRULE:FREQ=WEEKLY;WKST=MO;BYDAY=MO")->weekStart.isEmpty());
}

void TestRepeat::customLeavesOtherRulesAlone()
{
    const QDate start(2026, 10, 7);
    const QTimeZone york("America/New_York");
    for (const char *line :
         {"RRULE:FREQ=MONTHLY;BYMONTHDAY=1,15", "RRULE:FREQ=MONTHLY;BYDAY=3WE",
          "RRULE:FREQ=WEEKLY;BYHOUR=9", "RRULE:FREQ=HOURLY",
          "RRULE:FREQ=DAILY;COUNT=3;UNTIL=20261201", "RRULE:FREQ=YEARLY;BYMONTH=3",
          // Filters that only look like the start's own month or day.
          "RRULE:FREQ=DAILY;BYMONTH=10", "RRULE:FREQ=MONTHLY;BYMONTH=10",
          "RRULE:FREQ=DAILY;BYMONTHDAY=7", "RRULE:FREQ=WEEKLY;BYMONTHDAY=1,15",
          "RRULE:FREQ=WEEKLY;WKST=XX", "RRULE:FREQ=YEARLY;BYMONTHDAY=7"})
        QVERIFY2(!Repeat::custom({QString::fromLatin1(line)}, start, york), line);
    QVERIFY(!Repeat::custom({}, start, york));
}

void TestRepeat::customWritesItsRule()
{
    const QDate start(2026, 10, 7);
    const QTimeZone york("America/New_York");
    Repeat::Custom c;
    c.interval = 2;
    c.weekdays = {5, 1};
    c.until = QDate(2026, 12, 1);
    // The whole last day, in the event's zone.
    QCOMPARE(Repeat::rule(c, start, false, york),
             QStringList{u"RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,FR;UNTIL=20261202T045959Z"_s});
    QCOMPARE(Repeat::rule(c, start, true, york),
             QStringList{u"RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,FR;UNTIL=20261201"_s});
    Repeat::Custom monthly;
    monthly.frequency = u"monthly"_s;
    monthly.count = 3;
    QCOMPARE(Repeat::rule(monthly, start, false, york),
             QStringList{u"RRULE:FREQ=MONTHLY;BYMONTHDAY=7;COUNT=3"_s});
    // What it writes, it reads back the same, its days in order.
    c.weekdays = {1, 5};
    QCOMPARE(*Repeat::custom(Repeat::rule(c, start, false, york), start, york), c);
}

void TestRepeat::customInWords()
{
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    const QDate start(2026, 10, 7);
    Repeat::Custom c;
    c.interval = 2;
    c.weekdays = {1, 3, 5};
    c.count = 5;
    QCOMPARE(Repeat::describe(c, start),
             u"Every 2 weeks on Monday, Wednesday and Friday, 5 times"_s);
    Repeat::Custom monthly;
    monthly.frequency = u"monthly"_s;
    monthly.onWeekday = true;
    monthly.until = QDate(2027, 3, 3);
    QCOMPARE(Repeat::describe(monthly, start),
             u"Monthly on the first Wednesday, until Mar 3, 2027"_s);
}

QTEST_GUILESS_MAIN(TestRepeat)
#include "tst_repeat.moc"
