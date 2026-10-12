#include "callie/Ics.h"

#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestIcs : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void readsOneOffs();
    void allDayEndsTheDayAfter();
    void seriesSkipExceptionsAndTakeChanges();
    void guestsRemindersAndCalls();
    void writesWhatItReads();
    void removesOneOrAll();
    void movesOneOrAll();
    void seriesKeepTheirHourAcrossAClockChange();
    void allDaySeriesMoveByDays();
    void editsThisAndFollowingSplitTheSeries();
    void answersAsTheUser();
};

namespace {

const QTimeZone kBerlin("Europe/Berlin");
const QTimeZone kNewYork("America/New_York");

const ics::Calendar kHome{u"local:home"_s, QColor(u"#ff8800"_s), true, u"me@example.com"_s};

QByteArray calendar(const QByteArray &events)
{
    return "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//test//EN\r\n" + events +
           "END:VCALENDAR\r\n";
}

/// Standup at 9:00 Berlin time every weekday from Monday 5 October 2026, with
/// Wednesday skipped and Thursday moved to 10:00.
const QByteArray kStandup = calendar(R"(BEGIN:VEVENT
UID:standup
DTSTART;TZID=Europe/Berlin:20261005T090000
DTEND;TZID=Europe/Berlin:20261005T091500
RRULE:FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR
EXDATE;TZID=Europe/Berlin:20261007T090000
SUMMARY:Standup
END:VEVENT
BEGIN:VEVENT
UID:standup
RECURRENCE-ID;TZID=Europe/Berlin:20261008T090000
DTSTART;TZID=Europe/Berlin:20261008T100000
DTEND;TZID=Europe/Berlin:20261008T101500
SUMMARY:Standup, later
END:VEVENT
)");

QList<Event> week(const ics::Group &events, const QTimeZone &zone = kBerlin)
{
    const QDateTime monday(QDate(2026, 10, 5), QTime(0, 0), zone);
    QList<Event> all;
    for (const ics::Group &group : ics::byUid(events))
        all += ics::expand(group, kHome, monday, monday.addDays(7), zone);
    std::ranges::sort(all, {}, &Event::start);
    return all;
}

QStringList summaries(const QList<Event> &events)
{
    QStringList list;
    for (const Event &event : events)
        list << event.start.toString(u"ddd HH:mm"_s) + u' ' + event.summary;
    return list;
}

} // namespace

void TestIcs::readsOneOffs()
{
    const ics::Group events = ics::parse(calendar(R"(BEGIN:VEVENT
UID:lunch
DTSTART:20261006T110000Z
DTEND:20261006T120000Z
SUMMARY:Lunch
LOCATION:Cafe Sol
DESCRIPTION:Bring the notes
END:VEVENT
BEGIN:VTODO
UID:task
SUMMARY:Not an event
END:VTODO
)"));
    QCOMPARE(events.size(), 1);
    const QList<Event> found = week(events);
    QCOMPARE(found.size(), 1);
    const Event &lunch = found.first();
    QCOMPARE(lunch.summary, u"Lunch"_s);
    QCOMPARE(lunch.location, u"Cafe Sol"_s);
    QCOMPARE(lunch.description, u"Bring the notes"_s);
    QCOMPARE(lunch.start, QDateTime(QDate(2026, 10, 6), QTime(13, 0), kBerlin));
    QCOMPARE(lunch.end.timeZone(), kBerlin);
    QCOMPARE(lunch.calendarId, u"local:home"_s);
    QCOMPARE(lunch.color, QColor(u"#ff8800"_s));
    QCOMPARE(lunch.eventId, u"lunch"_s);
    QVERIFY(lunch.seriesId.isEmpty());
    QVERIFY(lunch.canEdit);

    QString error;
    QVERIFY(ics::parse("not a calendar", &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void TestIcs::allDayEndsTheDayAfter()
{
    const ics::Group events = ics::parse(calendar(R"(BEGIN:VEVENT
UID:trip
DTSTART;VALUE=DATE:20261006
DTEND;VALUE=DATE:20261008
SUMMARY:Trip
END:VEVENT
)"));
    // Midnight wherever the calendar is shown, whatever zone wrote it.
    const Event trip = week(events, kNewYork).value(0);
    QVERIFY(trip.allDay);
    QCOMPARE(trip.start, QDateTime(QDate(2026, 10, 6), QTime(0, 0), kNewYork));
    QCOMPARE(trip.end, QDateTime(QDate(2026, 10, 8), QTime(0, 0), kNewYork));
}

void TestIcs::seriesSkipExceptionsAndTakeChanges()
{
    const QList<Event> found = week(ics::parse(kStandup));
    QCOMPARE(summaries(found),
             (QStringList{u"Mon 09:00 Standup"_s, u"Tue 09:00 Standup"_s,
                          u"Thu 10:00 Standup, later"_s, u"Fri 09:00 Standup"_s}));
    const Event &moved = found.at(2);
    QVERIFY(moved.detached);
    QCOMPARE(moved.seriesId, u"standup"_s);
    QCOMPARE(moved.recurrenceId, QDateTime(QDate(2026, 10, 8), QTime(9, 0), kBerlin));
    QCOMPARE(found.first().recurrence, QStringList{u"RRULE:FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR"_s});
    QVERIFY(found.first().eventId != found.at(1).eventId);
    QCOMPARE(found.first().zone, u"Europe/Berlin"_s);
    // Seen from New York, still at nine in Berlin.
    QCOMPARE(week(ics::parse(kStandup), kNewYork).first().start.time(), QTime(3, 0));
}

void TestIcs::guestsRemindersAndCalls()
{
    const QList<Event> found = week(ics::parse(calendar(R"(BEGIN:VEVENT
UID:review
DTSTART:20261006T130000Z
DTEND:20261006T140000Z
SUMMARY:Review
ORGANIZER;CN=Sam:mailto:sam@example.com
ATTENDEE;CN=Sam;PARTSTAT=ACCEPTED:mailto:sam@example.com
ATTENDEE;PARTSTAT=TENTATIVE:mailto:ME@example.com
ATTENDEE;PARTSTAT=NEEDS-ACTION:mailto:alex@example.com
CONFERENCE;VALUE=URI;FEATURE=VIDEO:https://meet.example.com/abc
BEGIN:VALARM
ACTION:DISPLAY
TRIGGER:-PT10M
DESCRIPTION:Review
END:VALARM
END:VEVENT
)")));
    const Event review = found.value(0);
    QCOMPARE(review.guests.size(), 3);
    QVERIFY(review.guests.first().organizer);
    QCOMPARE(review.responseStatus, u"tentative"_s);
    QVERIFY(review.canRespond);
    QCOMPARE(review.attendees, (QStringList{u"sam@example.com"_s, u"alex@example.com"_s}));
    QCOMPARE(review.reminders, QList<int>{10});
    QVERIFY(review.remindersKnown);
    QCOMPARE(review.conferenceUrl, QUrl(u"https://meet.example.com/abc"_s));
}

void TestIcs::writesWhatItReads()
{
    const ics::Group again = ics::parse(ics::write(ics::parse(kStandup)));
    QCOMPARE(summaries(week(again)), summaries(week(ics::parse(kStandup))));
}

void TestIcs::removesOneOrAll()
{
    const ics::Group events = ics::parse(kStandup);
    const QList<Event> found = week(events);
    QString error;
    const auto tuesday = ics::remove(events, found.at(1), false, &error);
    QVERIFY(tuesday);
    QCOMPARE(summaries(week(tuesday->group)),
             (QStringList{u"Mon 09:00 Standup"_s, u"Thu 10:00 Standup, later"_s,
                          u"Fri 09:00 Standup"_s}));
    // The moved one goes for good, not back to where it was.
    const auto thursday = ics::remove(events, found.at(2), false, &error);
    QCOMPARE(week(thursday->group).size(), 3);
    QVERIFY(ics::remove(events, found.at(0), true, &error)->group.isEmpty());
}

void TestIcs::movesOneOrAll()
{
    const ics::Group events = ics::parse(kStandup);
    const QList<Event> found = week(events);
    QString error;
    const Event friday = found.at(3);
    const auto one = ics::move(events, friday, friday.start.addSecs(3600), friday.end.addSecs(3600),
                               false, &error);
    QVERIFY(one);
    QCOMPARE(summaries(week(one->group)).last(), u"Fri 10:00 Standup"_s);
    QCOMPARE(summaries(week(one->group)).first(), u"Mon 09:00 Standup"_s);

    // The whole series half an hour later, Thursday's change and Wednesday's
    // gap kept.
    const Event monday = found.at(0);
    const auto all = ics::move(events, monday, monday.start.addSecs(1800), monday.end.addSecs(1800),
                               true, &error);
    QVERIFY(all);
    QCOMPARE(summaries(week(all->group)),
             (QStringList{u"Mon 09:30 Standup"_s, u"Tue 09:30 Standup"_s,
                          u"Thu 10:00 Standup, later"_s, u"Fri 09:30 Standup"_s}));
}

void TestIcs::seriesKeepTheirHourAcrossAClockChange()
{
    // Berlin leaves summer time on Sunday 25 October 2026, between the
    // series' start and the occurrence moved.
    const ics::Group events = ics::parse(calendar(R"(BEGIN:VEVENT
UID:planning
DTSTART;TZID=Europe/Berlin:20261019T090000
DTEND;TZID=Europe/Berlin:20261019T093000
RRULE:FREQ=WEEKLY;BYDAY=MO
SUMMARY:Planning
END:VEVENT
)"));
    const QDateTime november(QDate(2026, 11, 2), QTime(0, 0), kBerlin);
    const Event moving =
        ics::expand(events, kHome, november, november.addDays(1), kBerlin).value(0);
    QCOMPARE(moving.start.time(), QTime(9, 0));

    // A week later, every one: still at nine on Berlin's clock, the first in
    // winter time and the rest too.
    QString error;
    const auto moved =
        ics::move(events, moving, moving.start.addDays(7), moving.end.addDays(7), true, &error);
    QVERIFY2(moved, qPrintable(error));
    const QDateTime from(QDate(2026, 10, 19), QTime(0, 0), kBerlin);
    const QList<Event> after = ics::expand(moved->group, kHome, from, from.addDays(21), kBerlin);
    QCOMPARE(after.size(), 2);
    QCOMPARE(after.first().start, QDateTime(QDate(2026, 10, 26), QTime(9, 0), kBerlin));
    QCOMPARE(after.last().start, QDateTime(QDate(2026, 11, 2), QTime(9, 0), kBerlin));

    // Two days on, to Wednesdays, and the rule's day goes with it.
    const Event first = after.first();
    const auto wednesdays =
        ics::move(moved->group, first, first.start.addDays(2), first.end.addDays(2), true, &error);
    for (const Event &event :
         ics::expand(wednesdays->group, kHome, from, from.addDays(28), kBerlin))
        QCOMPARE(event.start.date().dayOfWeek(), int(Qt::Wednesday));
}

void TestIcs::allDaySeriesMoveByDays()
{
    // Bins out on Mondays, not on the 12th, and the 19th's on the Tuesday.
    const ics::Group events = ics::parse(calendar(R"(BEGIN:VEVENT
UID:bins
DTSTART;VALUE=DATE:20261005
DTEND;VALUE=DATE:20261006
RRULE:FREQ=WEEKLY
EXDATE;VALUE=DATE:20261012
SUMMARY:Bins
END:VEVENT
BEGIN:VEVENT
UID:bins
RECURRENCE-ID;VALUE=DATE:20261019
DTSTART;VALUE=DATE:20261020
DTEND;VALUE=DATE:20261021
SUMMARY:Bins, late
END:VEVENT
)"));
    const QDateTime from(QDate(2026, 10, 5), QTime(0, 0), kBerlin);
    const auto days = [&from](const ics::Group &group) {
        QStringList list;
        for (const Event &event : ics::expand(group, kHome, from, from.addDays(28), kBerlin))
            list << event.start.date().toString(u"MMM d"_s) + u' ' + event.summary;
        list.sort();
        return list;
    };
    QCOMPARE(days(events),
             (QStringList{u"Oct 20 Bins, late"_s, u"Oct 26 Bins"_s, u"Oct 5 Bins"_s}));

    // A day later, the gap and the changed one move with it.
    QString error;
    const Event first = ics::expand(events, kHome, from, from.addDays(1), kBerlin).value(0);
    const auto moved =
        ics::move(events, first, first.start.addDays(1), first.end.addDays(1), true, &error);
    QVERIFY2(moved, qPrintable(error));
    QCOMPARE(days(moved->group),
             (QStringList{u"Oct 20 Bins, late"_s, u"Oct 27 Bins"_s, u"Oct 6 Bins"_s}));
}

void TestIcs::editsThisAndFollowingSplitTheSeries()
{
    const ics::Group events = ics::parse(kStandup);
    const QList<Event> found = week(events);
    EventEdit edit;
    edit.summary = u"Sync"_s;
    QString error;
    const auto split =
        ics::update(events, found.at(1), edit, EditScope::ThisAndFollowing, u"sync"_s, &error);
    QVERIFY2(split, qPrintable(error));
    QCOMPARE(summaries(week(split->group)), QStringList{u"Mon 09:00 Standup"_s});
    QCOMPARE(
        summaries(week(split->split)),
        (QStringList{u"Tue 09:00 Sync"_s, u"Thu 10:00 Standup, later"_s, u"Fri 09:00 Sync"_s}));
    QCOMPARE(split->split.first()->uid(), u"sync"_s);

    // Only this one, and every one.
    const auto one = ics::update(events, found.at(0), edit, EditScope::ThisEvent, {}, &error);
    QCOMPARE(summaries(week(one->group)).first(), u"Mon 09:00 Sync"_s);
    QCOMPARE(summaries(week(one->group)).at(1), u"Tue 09:00 Standup"_s);
    const auto all = ics::update(events, found.at(1), edit, EditScope::AllEvents, {}, &error);
    QCOMPARE(summaries(week(all->group)).at(3), u"Fri 09:00 Sync"_s);

    // A video call is not something a file can make.
    EventEdit call;
    call.videoCall = true;
    QVERIFY(!ics::update(events, found.at(0), call, EditScope::AllEvents, {}, &error));
    QVERIFY(!error.isEmpty());
}

void TestIcs::answersAsTheUser()
{
    const ics::Group events = ics::parse(calendar(R"(BEGIN:VEVENT
UID:review
DTSTART:20261006T130000Z
DTEND:20261006T140000Z
SUMMARY:Review
ORGANIZER:mailto:sam@example.com
ATTENDEE;PARTSTAT=NEEDS-ACTION;RSVP=TRUE:mailto:me@example.com
END:VEVENT
)"));
    QString error;
    const auto yes = ics::respond(events, week(events).first(), u"accepted"_s, false,
                                  u"me@example.com"_s, &error);
    QVERIFY(yes);
    QCOMPARE(week(yes->group).first().responseStatus, u"accepted"_s);
    QVERIFY(!ics::respond(events, week(events).first(), u"accepted"_s, false,
                          u"someone@example.com"_s, &error));
}

QTEST_MAIN(TestIcs)
#include "tst_ics.moc"
