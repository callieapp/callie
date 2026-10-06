#include "callie/QuickAdd.h"

#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

// Wednesday 7 October 2026, 13:40 in Berlin.
const QTimeZone kZone("Europe/Berlin");
const QDateTime kNow(QDate(2026, 10, 7), QTime(13, 40), kZone);

EventDraft parse(const QString &text)
{
    return QuickAdd::parse(text, kNow, kZone);
}

QDateTime at(int month, int day, int hour, int minute = 0)
{
    return QDateTime(QDate(2026, month, day), QTime(hour, minute), kZone);
}

} // namespace

class TestQuickAdd : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void titleDayRangeAndPlace();
    void times_data();
    void times();
    void days_data();
    void days();
    void dayWithoutTimeIsAllDay();
    void timeWithoutDayIsTheNextOne();
    void lengthSetsTheEnd();
    void nothingGivenStartsNextHour();
    void numbersInTheTitleStay();
    void tonightIsEvening();
    void versionNumbersStayInTheTitle();
    void impossibleDatesStayInTheTitle();
};

void TestQuickAdd::titleDayRangeAndPlace()
{
    const EventDraft draft = parse(u"Lunch with Alex tomorrow 12-1pm at Cafe Sol"_s);
    QCOMPARE(draft.summary, u"Lunch with Alex"_s);
    QCOMPARE(draft.location, u"Cafe Sol"_s);
    QCOMPARE(draft.start, at(10, 8, 12));
    QCOMPARE(draft.end, at(10, 8, 13));
    QVERIFY(!draft.allDay);
    QVERIFY(draft.isValid());
}

void TestQuickAdd::times_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QDateTime>("start");
    QTest::addColumn<QDateTime>("end");

    QTest::newRow("pm") << u"Call friday 3pm"_s << at(10, 9, 15) << at(10, 9, 16);
    QTest::newRow("minutes") << u"Call friday 3:30pm"_s << at(10, 9, 15, 30) << at(10, 9, 16, 30);
    QTest::newRow("24-hour") << u"Call friday 15:00"_s << at(10, 9, 15) << at(10, 9, 16);
    QTest::newRow("at bare hour") << u"Call friday at 4"_s << at(10, 9, 16) << at(10, 9, 17);
    QTest::newRow("morning guess") << u"Call friday at 9"_s << at(10, 9, 9) << at(10, 9, 10);
    QTest::newRow("noon") << u"Lunch friday at noon"_s << at(10, 9, 12) << at(10, 9, 13);
    QTest::newRow("range shares pm") << u"Review friday 3-4pm"_s << at(10, 9, 15) << at(10, 9, 16);
    QTest::newRow("range crosses noon")
        << u"Review friday 11-1pm"_s << at(10, 9, 11) << at(10, 9, 13);
    QTest::newRow("from to") << u"Workshop friday from 2 to 4:30"_s << at(10, 9, 14)
                             << at(10, 9, 16, 30);
    QTest::newRow("words") << u"Gig friday 9pm to 11pm"_s << at(10, 9, 21) << at(10, 9, 23);
    QTest::newRow("compact") << u"Test event friday 4pm to 415pm"_s << at(10, 9, 16)
                             << at(10, 9, 16, 15);
    QTest::newRow("compact range")
        << u"Call friday 945-1015am"_s << at(10, 9, 9, 45) << at(10, 9, 10, 15);
    QTest::newRow("dotted") << u"Call friday at 4.30pm"_s << at(10, 9, 16, 30) << at(10, 9, 17, 30);
    QTest::newRow("dotted range") << u"Call friday 4.15-5"_s << at(10, 9, 16, 15) << at(10, 9, 17);
    QTest::newRow("past midnight") << u"Party friday 10pm-1am"_s << at(10, 9, 22) << at(10, 10, 1);
}

void TestQuickAdd::times()
{
    QFETCH(QString, text);
    QFETCH(QDateTime, start);
    QFETCH(QDateTime, end);

    const EventDraft draft = parse(text);
    QCOMPARE(draft.start, start);
    QCOMPARE(draft.end, end);
    QVERIFY(!draft.allDay);
}

void TestQuickAdd::days_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QDate>("date");

    QTest::newRow("today") << u"Gym today 6pm"_s << QDate(2026, 10, 7);
    QTest::newRow("tomorrow") << u"Gym tomorrow 6pm"_s << QDate(2026, 10, 8);
    QTest::newRow("weekday") << u"Gym monday 6pm"_s << QDate(2026, 10, 12);
    QTest::newRow("same weekday is next week") << u"Gym wednesday 6pm"_s << QDate(2026, 10, 14);
    QTest::newRow("next weekday") << u"Gym next monday 6pm"_s << QDate(2026, 10, 19);
    QTest::newRow("on weekday") << u"Gym on fri 6pm"_s << QDate(2026, 10, 9);
    QTest::newRow("in days") << u"Gym in 3 days 6pm"_s << QDate(2026, 10, 10);
    QTest::newRow("in weeks") << u"Gym in 2 weeks 6pm"_s << QDate(2026, 10, 21);
    QTest::newRow("month day") << u"Gym oct 20 6pm"_s << QDate(2026, 10, 20);
    QTest::newRow("day month") << u"Gym 20th of november 6pm"_s << QDate(2026, 11, 20);
    QTest::newRow("passed rolls over") << u"Gym march 3 6pm"_s << QDate(2027, 3, 3);
    QTest::newRow("iso") << u"Gym 2026-12-24 6pm"_s << QDate(2026, 12, 24);
}

void TestQuickAdd::days()
{
    QFETCH(QString, text);
    QFETCH(QDate, date);

    const EventDraft draft = parse(text);
    QCOMPARE(draft.summary, u"Gym"_s);
    QCOMPARE(draft.start, QDateTime(date, QTime(18, 0), kZone));
}

void TestQuickAdd::dayWithoutTimeIsAllDay()
{
    const EventDraft draft = parse(u"Mum's birthday oct 20"_s);
    QCOMPARE(draft.summary, u"Mum's birthday"_s);
    QVERIFY(draft.allDay);
    QCOMPARE(draft.start, at(10, 20, 0));
    QCOMPARE(draft.end, at(10, 21, 0));
}

void TestQuickAdd::timeWithoutDayIsTheNextOne()
{
    // 15:00 is still ahead today; 9:00 has passed, so it is tomorrow's.
    QCOMPARE(parse(u"Tea at 3pm"_s).start, at(10, 7, 15));
    QCOMPARE(parse(u"Run at 9am"_s).start, at(10, 8, 9));
}

void TestQuickAdd::lengthSetsTheEnd()
{
    const EventDraft half = parse(u"Standup friday 9:30am for 15 min"_s);
    QCOMPARE(half.summary, u"Standup"_s);
    QCOMPARE(half.end, at(10, 9, 9, 45));

    const EventDraft hours = parse(u"Hike saturday 8am for 2.5 hours"_s);
    QCOMPARE(hours.end, at(10, 10, 10, 30));
}

void TestQuickAdd::nothingGivenStartsNextHour()
{
    const EventDraft draft = parse(u"Think about the roadmap"_s);
    QCOMPARE(draft.summary, u"Think about the roadmap"_s);
    QCOMPARE(draft.start, at(10, 7, 14));
    QCOMPARE(draft.end, at(10, 7, 15));
    QVERIFY(draft.location.isEmpty());
}

void TestQuickAdd::numbersInTheTitleStay()
{
    const EventDraft draft = parse(u"Plan Q4 with 3 teams friday 2pm @ Room 12"_s);
    QCOMPARE(draft.summary, u"Plan Q4 with 3 teams"_s);
    QCOMPARE(draft.location, u"Room 12"_s);
    QCOMPARE(draft.start, at(10, 9, 14));
}

void TestQuickAdd::tonightIsEvening()
{
    const EventDraft draft = parse(u"Dinner tonight"_s);
    QCOMPARE(draft.summary, u"Dinner"_s);
    QCOMPARE(draft.start, at(10, 7, 19));
}

void TestQuickAdd::impossibleDatesStayInTheTitle()
{
    for (const QString &text :
         {u"Party feb 30"_s, u"Party 2026-13-45"_s, u"Party 31st of april"_s}) {
        const EventDraft draft = parse(text);
        QCOMPARE(draft.summary, text);
        QVERIFY(!draft.allDay);
    }
    for (const QString &text : {u"Party at 25"_s, u"Party at 9:99"_s, u"Party 23-26"_s}) {
        const EventDraft draft = parse(text);
        QCOMPARE(draft.summary, text);
        QCOMPARE(draft.start, at(10, 7, 14)); // the next whole hour, as with no time
    }
    // February 29 exists only in leap years; 2027 is not one.
    QCOMPARE(parse(u"Leap feb 29"_s).summary, u"Leap feb 29"_s);
}

void TestQuickAdd::versionNumbersStayInTheTitle()
{
    const EventDraft draft = parse(u"Ship 4.15 friday 3pm"_s);
    QCOMPARE(draft.summary, u"Ship 4.15"_s);
    QCOMPARE(draft.start, at(10, 9, 15));

    // A span of years is not a time range.
    const EventDraft years = parse(u"Budget 2025-26 review friday 3pm"_s);
    QCOMPARE(years.summary, u"Budget 2025-26 review"_s);
    QCOMPARE(years.start, at(10, 9, 15));

    // Nor is a price range, or a dotted number followed by a count.
    QCOMPARE(parse(u"Lunch $5-9.50 friday 1pm"_s).summary, u"Lunch $5-9.50"_s);
    const EventDraft shipped = parse(u"Ship 4.15-26 friday 3pm"_s);
    QCOMPARE(shipped.summary, u"Ship 4.15-26"_s);
    QCOMPARE(shipped.start, at(10, 9, 15));

    // "to" only joins a range after a time.
    const EventDraft upgrade = parse(u"Upgrade to 4.15 friday 3pm"_s);
    QCOMPARE(upgrade.summary, u"Upgrade to 4.15"_s);
    QCOMPARE(upgrade.start, at(10, 9, 15));
    QCOMPARE(parse(u"Call friday 3 to 4.30"_s).end, at(10, 9, 16, 30));

    // A place needs letters.
    const EventDraft fee = parse(u"Fee at $50"_s);
    QCOMPARE(fee.summary, u"Fee at $50"_s);
    QVERIFY(fee.location.isEmpty());

    // Nor is a time that cannot exist, even written with a dot.
    QCOMPARE(parse(u"Party at 9.99pm"_s).summary, u"Party at 9.99pm"_s);
}

QTEST_GUILESS_MAIN(TestQuickAdd)
#include "tst_quickadd.moc"
