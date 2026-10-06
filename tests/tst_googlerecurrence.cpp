#include "callie/GoogleRecurrence.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

const QTimeZone kNewYork("America/New_York");
const QTimeZone kBerlin("Europe/Berlin");

GoogleEvent parsed(const char *json)
{
    return parseGoogleEvent(QJsonDocument::fromJson(json).object());
}

QDateTime utc(int y, int m, int d, int h = 0, int min = 0)
{
    return QDateTime(QDate(y, m, d), QTime(h, min), QTimeZone::UTC);
}

QList<Event> expand(const QList<GoogleEvent> &events, const QDateTime &from, const QDateTime &to)
{
    QList<Event> result = expandGoogleEvents(events, from, to, kNewYork);
    std::sort(result.begin(), result.end(),
              [](const Event &a, const Event &b) { return a.start < b.start; });
    return result;
}

// Weekly on Mondays at 09:30 New York time, from Monday 2026-10-05.
const char *const kStandup = R"({"id":"standup","summary":"Standup",
    "start":{"dateTime":"2026-10-05T09:30:00-04:00","timeZone":"America/New_York"},
    "end":{"dateTime":"2026-10-05T09:45:00-04:00","timeZone":"America/New_York"},
    "recurrence":["RRULE:FREQ=WEEKLY;BYDAY=MO"]})";

} // namespace

class TestGoogleRecurrence : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void declinedOccurrenceIsMarked();
    void singleEventOverlapsRange();
    void weeklySeriesKeepsWallClockAcrossDst();
    void countLimitsSeries();
    void exdateWithZoneRemovesOccurrence();
    void cancelledOccurrenceIsSkipped();
    void movedOccurrenceReplacesOriginal();
    void occurrenceMovedIntoRangeAppears();
    void allDaySeriesRecursOnDates();
    void occurrenceStartedBeforeRangeIsIncluded();
    void occurrencesTouchingRangeEdgesAreExcluded();
    void cancelledEventWithTimesIsHidden();
    void endlessSeriesIsBounded();
    void allDayExceptionsMatchByDate();
    void rdateAddsOccurrence();
    void rdatesAloneFormASeries();
    void utcExdateRemovesOccurrence();
    void dateExdateRemovesOccurrence();
    void allDayStartsAtMidnightInViewZone();
    void unreadableRuleFallsBackToFirstOccurrence();
};

void TestGoogleRecurrence::singleEventOverlapsRange()
{
    const GoogleEvent lunch = parsed(R"({"id":"lunch",
        "start":{"dateTime":"2026-10-06T12:00:00-04:00"},
        "end":{"dateTime":"2026-10-06T13:00:00-04:00"}})");

    QCOMPARE(expand({lunch}, utc(2026, 10, 6), utc(2026, 10, 7)).size(), 1);
    QCOMPARE(expand({lunch}, utc(2026, 10, 6, 17), utc(2026, 10, 7)).size(), 0);
    QCOMPARE(expand({lunch}, utc(2026, 10, 5), utc(2026, 10, 6, 16)).size(), 0);
}

void TestGoogleRecurrence::weeklySeriesKeepsWallClockAcrossDst()
{
    // New York leaves daylight time on 2026-11-01, between these Mondays.
    const QList<Event> events = expand({parsed(kStandup)}, utc(2026, 10, 26), utc(2026, 11, 3));

    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(0).start.toTimeZone(kNewYork).time(), QTime(9, 30));
    QCOMPARE(events.at(1).start.toTimeZone(kNewYork).time(), QTime(9, 30));
    QCOMPARE(events.at(0).start.toUTC(), utc(2026, 10, 26, 13, 30));
    QCOMPARE(events.at(1).start.toUTC(), utc(2026, 11, 2, 14, 30));
    QCOMPARE(events.at(1).start.secsTo(events.at(1).end), 15 * 60);
    QCOMPARE(events.at(1).uid, u"standup"_s);
    QCOMPARE(events.at(1).recurrenceId, events.at(1).start);
}

void TestGoogleRecurrence::countLimitsSeries()
{
    const GoogleEvent series = parsed(R"({"id":"s",
        "start":{"dateTime":"2026-10-05T10:00:00Z"},"end":{"dateTime":"2026-10-05T11:00:00Z"},
        "recurrence":["RRULE:FREQ=DAILY;COUNT=3"]})");

    QCOMPARE(expand({series}, utc(2026, 10, 1), utc(2026, 11, 1)).size(), 3);
}

void TestGoogleRecurrence::exdateWithZoneRemovesOccurrence()
{
    const GoogleEvent series = parsed(R"({"id":"s",
        "start":{"dateTime":"2026-10-05T12:00:00+02:00","timeZone":"Europe/Berlin"},
        "end":{"dateTime":"2026-10-05T13:00:00+02:00","timeZone":"Europe/Berlin"},
        "recurrence":["RRULE:FREQ=WEEKLY","EXDATE;TZID=Europe/Berlin:20261012T120000"]})");

    const QList<Event> events = expand({series}, utc(2026, 10, 1), utc(2026, 10, 20));

    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(0).start.date(), QDate(2026, 10, 5));
    QCOMPARE(events.at(1).start.date(), QDate(2026, 10, 19));
    QCOMPARE(events.at(1).start.toTimeZone(kBerlin).time(), QTime(12, 0));
}

void TestGoogleRecurrence::cancelledOccurrenceIsSkipped()
{
    const GoogleEvent cancelled = parsed(R"({"id":"standup_20261012T133000Z","status":"cancelled",
        "recurringEventId":"standup",
        "originalStartTime":{"dateTime":"2026-10-12T09:30:00-04:00",
                             "timeZone":"America/New_York"}})");

    const QList<Event> events =
        expand({parsed(kStandup), cancelled}, utc(2026, 10, 5), utc(2026, 10, 20));

    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(0).start.date(), QDate(2026, 10, 5));
    QCOMPARE(events.at(1).start.date(), QDate(2026, 10, 19));
}

void TestGoogleRecurrence::movedOccurrenceReplacesOriginal()
{
    const GoogleEvent moved =
        parsed(R"json({"id":"standup_20261012T133000Z","summary":"Standup (late)",
        "recurringEventId":"standup",
        "originalStartTime":{"dateTime":"2026-10-12T09:30:00-04:00"},
        "start":{"dateTime":"2026-10-12T11:00:00-04:00","timeZone":"America/New_York"},
        "end":{"dateTime":"2026-10-12T11:30:00-04:00","timeZone":"America/New_York"}})json");

    const QList<Event> events =
        expand({parsed(kStandup), moved}, utc(2026, 10, 12), utc(2026, 10, 13));

    QCOMPARE(events.size(), 1);
    QCOMPARE(events.first().summary, u"Standup (late)"_s);
    QCOMPARE(events.first().start.toUTC(), utc(2026, 10, 12, 15));
    QCOMPARE(events.first().uid, u"standup"_s);
    QCOMPARE(events.first().recurrenceId.toUTC(), utc(2026, 10, 12, 13, 30));
}

void TestGoogleRecurrence::occurrenceMovedIntoRangeAppears()
{
    // Originally Monday the 12th, moved to Wednesday the 14th.
    const GoogleEvent moved = parsed(R"({"id":"standup_20261012T133000Z",
        "recurringEventId":"standup",
        "originalStartTime":{"dateTime":"2026-10-12T09:30:00-04:00"},
        "start":{"dateTime":"2026-10-14T09:30:00-04:00"},
        "end":{"dateTime":"2026-10-14T09:45:00-04:00"}})");

    const QList<Event> events =
        expand({parsed(kStandup), moved}, utc(2026, 10, 13), utc(2026, 10, 15));

    QCOMPARE(events.size(), 1);
    QCOMPARE(events.first().start.toUTC(), utc(2026, 10, 14, 13, 30));
}

void TestGoogleRecurrence::allDaySeriesRecursOnDates()
{
    const GoogleEvent birthday = parsed(R"({"id":"bday",
        "start":{"date":"2025-10-07"},"end":{"date":"2025-10-08"},
        "recurrence":["RRULE:FREQ=YEARLY"]})");

    const QList<Event> events = expand({birthday}, utc(2026, 10, 1), utc(2026, 11, 1));

    QCOMPARE(events.size(), 1);
    QVERIFY(events.first().allDay);
    QCOMPARE(events.first().start, QDateTime(QDate(2026, 10, 7), QTime(0, 0), kNewYork));
    QCOMPARE(events.first().end, QDateTime(QDate(2026, 10, 8), QTime(0, 0), kNewYork));
}

void TestGoogleRecurrence::occurrenceStartedBeforeRangeIsIncluded()
{
    const GoogleEvent overnight = parsed(R"({"id":"night",
        "start":{"dateTime":"2026-10-05T22:00:00Z"},"end":{"dateTime":"2026-10-06T06:00:00Z"},
        "recurrence":["RRULE:FREQ=DAILY"]})");

    const QList<Event> events = expand({overnight}, utc(2026, 10, 7), utc(2026, 10, 7, 12));

    QCOMPARE(events.size(), 1);
    QCOMPARE(events.first().start, utc(2026, 10, 6, 22));
}

void TestGoogleRecurrence::occurrencesTouchingRangeEdgesAreExcluded()
{
    const GoogleEvent daily = parsed(R"({"id":"d",
        "start":{"dateTime":"2026-10-05T22:00:00Z"},"end":{"dateTime":"2026-10-06T06:00:00Z"},
        "recurrence":["RRULE:FREQ=DAILY"]})");

    // One occurrence ends exactly at the start of the range, the next starts at its end.
    QVERIFY(expand({daily}, utc(2026, 10, 7, 6), utc(2026, 10, 7, 22)).isEmpty());
}

void TestGoogleRecurrence::cancelledEventWithTimesIsHidden()
{
    const GoogleEvent cancelledSeries = parsed(R"({"id":"s","status":"cancelled",
        "start":{"dateTime":"2026-10-05T10:00:00Z"},"end":{"dateTime":"2026-10-05T11:00:00Z"},
        "recurrence":["RRULE:FREQ=DAILY"]})");
    const GoogleEvent cancelledOccurrence = parsed(R"({"id":"standup_x","status":"cancelled",
        "recurringEventId":"standup",
        "originalStartTime":{"dateTime":"2026-10-12T09:30:00-04:00"},
        "start":{"dateTime":"2026-10-12T09:30:00-04:00"},
        "end":{"dateTime":"2026-10-12T09:45:00-04:00"}})");

    QVERIFY(expand({cancelledSeries, cancelledOccurrence}, utc(2026, 10, 5), utc(2026, 10, 20))
                .isEmpty());
}

void TestGoogleRecurrence::endlessSeriesIsBounded()
{
    const GoogleEvent daily = parsed(R"({"id":"d",
        "start":{"dateTime":"2000-01-01T08:00:00Z"},"end":{"dateTime":"2000-01-01T08:30:00Z"},
        "recurrence":["RRULE:FREQ=DAILY"]})");

    QElapsedTimer timer;
    timer.start();
    const QList<Event> events = expand({daily}, utc(2026, 10, 5), utc(2026, 10, 12));

    QCOMPARE(events.size(), 7);
    QVERIFY2(timer.elapsed() < 1000, "expansion should not walk every past occurrence slowly");
}

void TestGoogleRecurrence::unreadableRuleFallsBackToFirstOccurrence()
{
    const GoogleEvent broken = parsed(R"({"id":"b",
        "start":{"dateTime":"2026-10-05T10:00:00Z"},"end":{"dateTime":"2026-10-05T11:00:00Z"},
        "recurrence":["RRULE:FREQ=SOMETIMES"]})");

    QCOMPARE(expand({broken}, utc(2026, 10, 1), utc(2026, 11, 1)).size(), 1);
}

void TestGoogleRecurrence::allDayExceptionsMatchByDate()
{
    const GoogleEvent series = parsed(R"({"id":"gym","summary":"Gym",
        "start":{"date":"2026-10-05"},"end":{"date":"2026-10-06"},
        "recurrence":["RRULE:FREQ=DAILY;COUNT=4"]})");
    const GoogleEvent cancelled = parsed(R"({"id":"gym_20261006","status":"cancelled",
        "recurringEventId":"gym","originalStartTime":{"date":"2026-10-06"}})");
    const GoogleEvent moved = parsed(R"json({"id":"gym_20261007","summary":"Gym (moved)",
        "recurringEventId":"gym","originalStartTime":{"date":"2026-10-07"},
        "start":{"date":"2026-10-10"},"end":{"date":"2026-10-11"}
})json");

    const QList<Event> events =
        expand({series, cancelled, moved}, utc(2026, 10, 1), utc(2026, 10, 15));

    QStringList days;
    for (const Event &e : events)
        days.append(e.start.date().toString(Qt::ISODate) + u' ' + e.summary);
    QCOMPARE(days,
             (QStringList{u"2026-10-05 Gym"_s, u"2026-10-08 Gym"_s, u"2026-10-10 Gym (moved)"_s}));
}

void TestGoogleRecurrence::rdateAddsOccurrence()
{
    const GoogleEvent series = parsed(R"({"id":"s",
        "start":{"dateTime":"2026-10-05T12:00:00+02:00","timeZone":"Europe/Berlin"},
        "end":{"dateTime":"2026-10-05T13:00:00+02:00","timeZone":"Europe/Berlin"},
        "recurrence":["RRULE:FREQ=DAILY;COUNT=1","RDATE;TZID=Europe/Berlin:20261009T150000"]})");

    const QList<Event> events = expand({series}, utc(2026, 10, 1), utc(2026, 10, 15));

    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(1).start.toUTC(), utc(2026, 10, 9, 13));
}

void TestGoogleRecurrence::rdatesAloneFormASeries()
{
    const GoogleEvent series = parsed(R"({"id":"s",
        "start":{"dateTime":"2026-10-05T10:00:00Z"},"end":{"dateTime":"2026-10-05T11:00:00Z"},
        "recurrence":["RDATE:20261008T100000Z,20261012T100000Z"]})");

    const QList<Event> events = expand({series}, utc(2026, 10, 1), utc(2026, 10, 15));

    QCOMPARE(events.size(), 3);
    QCOMPARE(events.at(2).start, utc(2026, 10, 12, 10));
}

void TestGoogleRecurrence::utcExdateRemovesOccurrence()
{
    const GoogleEvent series = parsed(R"({"id":"s",
        "start":{"dateTime":"2026-10-05T10:00:00Z"},"end":{"dateTime":"2026-10-05T11:00:00Z"},
        "recurrence":["RRULE:FREQ=DAILY;COUNT=3","EXDATE:20261006T100000Z"]})");

    const QList<Event> events = expand({series}, utc(2026, 10, 1), utc(2026, 10, 15));

    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(1).start, utc(2026, 10, 7, 10));
}

void TestGoogleRecurrence::dateExdateRemovesOccurrence()
{
    const GoogleEvent series = parsed(R"({"id":"s",
        "start":{"date":"2026-10-05"},"end":{"date":"2026-10-06"},
        "recurrence":["RRULE:FREQ=DAILY;COUNT=3","EXDATE;VALUE=DATE:20261006"]})");

    const QList<Event> events = expand({series}, utc(2026, 10, 1), utc(2026, 10, 15));

    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(1).start.date(), QDate(2026, 10, 7));
}

void TestGoogleRecurrence::allDayStartsAtMidnightInViewZone()
{
    // Far from both UTC, which the test process uses, and New York.
    const QTimeZone tokyo("Asia/Tokyo");
    const GoogleEvent holiday = parsed(R"({"id":"h",
        "start":{"date":"2026-10-12"},"end":{"date":"2026-10-13"},
        "recurrence":["RRULE:FREQ=YEARLY"]})");

    const QList<Event> events =
        expandGoogleEvents({holiday}, QDateTime(QDate(2026, 10, 12), QTime(0, 0), tokyo),
                           QDateTime(QDate(2026, 10, 13), QTime(0, 0), tokyo), tokyo);

    QCOMPARE(events.size(), 1);
    QCOMPARE(events.first().start, QDateTime(QDate(2026, 10, 12), QTime(0, 0), tokyo));
    QCOMPARE(events.first().start.toUTC(), utc(2026, 10, 11, 15));
    QCOMPARE(events.first().end, QDateTime(QDate(2026, 10, 13), QTime(0, 0), tokyo));
}

void TestGoogleRecurrence::declinedOccurrenceIsMarked()
{
    // Declining one Monday arrives as an exception with the user's answer.
    const GoogleEvent declined = parsed(R"({"id":"standup_1","recurringEventId":"standup",
        "originalStartTime":{"dateTime":"2026-10-12T09:30:00-04:00","timeZone":"America/New_York"},
        "start":{"dateTime":"2026-10-12T09:30:00-04:00"},"end":{"dateTime":"2026-10-12T09:45:00-04:00"},
        "attendees":[{"email":"me@example.com","self":true,"responseStatus":"declined"}]})");
    const QList<Event> events =
        expand({parsed(kStandup), declined}, utc(2026, 10, 5), utc(2026, 10, 20));

    QCOMPARE(events.size(), 3);
    QVERIFY(!events.at(0).declined);
    QVERIFY(events.at(1).declined);
    QVERIFY(!events.at(2).declined);
}

QTEST_GUILESS_MAIN(TestGoogleRecurrence)
#include "tst_googlerecurrence.moc"
