#include "callie/Repeat.h"

#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestRepeat : public QObject
{
    Q_OBJECT

private Q_SLOTS:
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

QTEST_GUILESS_MAIN(TestRepeat)
#include "tst_repeat.moc"
