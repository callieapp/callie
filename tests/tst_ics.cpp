#include "callie/GoogleCalendarApi.h"
#include "callie/Ics.h"

#include <QJsonArray>
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
    void endsAndAddedDatesMoveWithTheSeries();
    void monthlySeriesFollowTheirNewDay();
    void editsThisAndFollowingSplitTheSeries();
    void answersAsTheUser();
    void theOrganizerIsTheUser();
    void eventsGoToGoogleWithTheirRepeats();
    void googleEventsBecomeICalendar();
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

void TestIcs::endsAndAddedDatesMoveWithTheSeries()
{
    // Mondays until 30 November, and one Thursday added.
    const ics::Group events = ics::parse(calendar(R"(BEGIN:VEVENT
UID:class
DTSTART;VALUE=DATE:20261005
RRULE:FREQ=WEEKLY;BYDAY=MO;UNTIL=20261130
RDATE;VALUE=DATE:20261015
SUMMARY:Class
END:VEVENT
)"));
    const QDateTime from(QDate(2026, 10, 1), QTime(0, 0), kBerlin);
    const Event first = ics::expand(events, kHome, from, from.addDays(5), kBerlin).value(0);
    QString error;
    const auto moved =
        ics::move(events, first, first.start.addDays(7), first.end.addDays(7), true, &error);
    QVERIFY2(moved, qPrintable(error));
    QList<QDate> days;
    for (const Event &event : ics::expand(moved->group, kHome, from, from.addMonths(3), kBerlin))
        days << event.start.date();
    std::ranges::sort(days);
    // Still nine Mondays, a week later, and the Thursday a week later too.
    QCOMPARE(days.size(), 10);
    QCOMPARE(days.first(), QDate(2026, 10, 12));
    QVERIFY(days.contains(QDate(2026, 10, 22)));
    QCOMPARE(days.last(), QDate(2026, 12, 7));
}

void TestIcs::monthlySeriesFollowTheirNewDay()
{
    // Moves every occurrence as the one on `from` moves to `to`, and gives the
    // days the series then falls on, from its first month, or the error.
    const auto movedTo = [](const QByteArray &rule, QDate first, QDate from, QDate to) {
        const ics::Group events =
            ics::parse(calendar("BEGIN:VEVENT\r\nUID:m\r\nDTSTART;VALUE=DATE:" +
                                first.toString(u"yyyyMMdd"_s).toLatin1() + "\r\nRRULE:" + rule +
                                "\r\nSUMMARY:Monthly\r\nEND:VEVENT\r\n"));
        const QDateTime day(from, QTime(0, 0), kBerlin);
        const Event moving = ics::expand(events, kHome, day, day.addDays(1), kBerlin).value(0);
        QString error;
        const auto moved = ics::move(events, moving, QDateTime(to, QTime(0, 0), kBerlin),
                                     QDateTime(to.addDays(1), QTime(0, 0), kBerlin), true, &error);
        QStringList days;
        if (!moved)
            return QStringList{error};
        const QDateTime month(QDate(first.year(), first.month(), 1), QTime(0, 0), kBerlin);
        for (const Event &event :
             ics::expand(moved->group, kHome, month, month.addMonths(4), kBerlin))
            days << event.start.date().toString(Qt::ISODate);
        return days;
    };
    // The second Monday, its 12 October moved to Wednesday 14 October: the
    // second Wednesday from then on, and before.
    QCOMPARE(movedTo("FREQ=MONTHLY;BYDAY=2MO", QDate(2026, 9, 14), QDate(2026, 10, 12),
                     QDate(2026, 10, 14)),
             (QStringList{u"2026-09-09"_s, u"2026-10-14"_s, u"2026-11-11"_s, u"2026-12-09"_s}));
    // The 28th, its first moved to 2 November: the 2nd, from November, with
    // the month it skipped skipped still.
    QCOMPARE(movedTo("FREQ=MONTHLY;BYMONTHDAY=28\r\nEXDATE;VALUE=DATE:20261128",
                     QDate(2026, 10, 28), QDate(2026, 10, 28), QDate(2026, 11, 2)),
             (QStringList{u"2026-11-02"_s, u"2027-01-02"_s}));
    // What cannot be carried over is refused, not left to repeat elsewhere.
    const QStringList ordinals = movedTo("FREQ=MONTHLY;BYDAY=1MO,3MO", QDate(2026, 10, 5),
                                         QDate(2026, 10, 5), QDate(2026, 10, 6));
    QVERIFY2(ordinals.value(0).contains(u"cannot move"_s), qPrintable(ordinals.join(u' ')));
    const QStringList yearly = movedTo("FREQ=YEARLY;BYMONTH=10;BYMONTHDAY=28", QDate(2026, 10, 28),
                                       QDate(2026, 10, 28), QDate(2026, 11, 2));
    QVERIFY2(yearly.value(0).contains(u"cannot move"_s), qPrintable(yearly.join(u' ')));

    // Days some months lack would skip them, so they are refused too.
    QVERIFY(movedTo("FREQ=MONTHLY;BYMONTHDAY=10", QDate(2026, 10, 10), QDate(2026, 10, 10),
                    QDate(2026, 10, 30))
                .value(0)
                .contains(u"cannot move"_s));
    QVERIFY(movedTo("FREQ=MONTHLY;BYDAY=4MO", QDate(2026, 10, 26), QDate(2026, 10, 26),
                    QDate(2026, 10, 29))
                .value(0)
                .contains(u"cannot move"_s));
    // A monthly rule naming no day repeats on its first one's date, which
    // moves to where the one moved lands.
    QCOMPARE(movedTo("FREQ=MONTHLY", QDate(2026, 10, 10), QDate(2026, 11, 10), QDate(2026, 12, 2)),
             (QStringList{u"2026-11-02"_s, u"2026-12-02"_s, u"2027-01-02"_s}));
    QVERIFY(movedTo("FREQ=MONTHLY", QDate(2026, 10, 10), QDate(2026, 11, 10), QDate(2026, 11, 30))
                .value(0)
                .contains(u"cannot move"_s));
    QCOMPARE(movedTo("FREQ=YEARLY", QDate(2026, 10, 10), QDate(2026, 10, 10), QDate(2026, 12, 2))
                 .value(0),
             u"2026-12-02"_s);

    // An end that is no occurrence's day ends the series as before: on the
    // last one, moved.
    QCOMPARE(movedTo("FREQ=MONTHLY;BYMONTHDAY=28;UNTIL=20261227", QDate(2026, 10, 28),
                     QDate(2026, 10, 28), QDate(2026, 11, 2)),
             (QStringList{u"2026-11-02"_s, u"2026-12-02"_s}));
    // A date added by hand has no place in a series moved by months.
    QVERIFY(movedTo("FREQ=MONTHLY;BYMONTHDAY=28\r\nRDATE;VALUE=DATE:20261015", QDate(2026, 10, 28),
                    QDate(2026, 10, 28), QDate(2026, 11, 2))
                .value(0)
                .contains(u"cannot move"_s));

    // The last Saturday dropped on 3 October would be the fifth from the end.
    QVERIFY(movedTo("FREQ=MONTHLY;BYDAY=-1SA", QDate(2026, 10, 31), QDate(2026, 10, 31),
                    QDate(2026, 10, 3))
                .value(0)
                .contains(u"cannot move"_s));
    // A weekend every other week stays in one week: a day earlier is fine,
    // a day later would split it.
    const QStringList earlier = movedTo("FREQ=WEEKLY;INTERVAL=2;BYDAY=SA,SU", QDate(2026, 10, 3),
                                        QDate(2026, 10, 3), QDate(2026, 10, 2));
    QCOMPARE(earlier.mid(0, 3), (QStringList{u"2026-10-02"_s, u"2026-10-03"_s, u"2026-10-16"_s}));
    QVERIFY(movedTo("FREQ=WEEKLY;INTERVAL=2;BYDAY=SA,SU", QDate(2026, 10, 3), QDate(2026, 10, 3),
                    QDate(2026, 10, 4))
                .value(0)
                .contains(u"cannot move"_s));

    // One changed on its own moves with its month.
    const ics::Group tenth =
        ics::parse(calendar("BEGIN:VEVENT\r\nUID:x\r\nDTSTART;VALUE=DATE:20261010\r\n"
                            "RRULE:FREQ=MONTHLY;BYMONTHDAY=10\r\nSUMMARY:Rent\r\nEND:VEVENT\r\n"
                            "BEGIN:VEVENT\r\nUID:x\r\nRECURRENCE-ID;VALUE=DATE:20261110\r\n"
                            "DTSTART;VALUE=DATE:20261111\r\nSUMMARY:Rent, late\r\nEND:VEVENT\r\n"));
    const QDateTime october(QDate(2026, 10, 1), QTime(0, 0), kBerlin);
    const Event rent = ics::expand(tenth, kHome, october, october.addDays(15), kBerlin).value(0);
    QString moveError;
    const auto twelfth =
        ics::move(tenth, rent, rent.start.addDays(2), rent.end.addDays(2), true, &moveError);
    QVERIFY2(twelfth, qPrintable(moveError));
    QStringList after;
    for (const Event &event :
         ics::expand(twelfth->group, kHome, october, october.addMonths(3), kBerlin))
        after << event.start.date().toString(Qt::ISODate) + u' ' + event.summary;
    after.sort();
    QCOMPARE(after,
             (QStringList{u"2026-10-12 Rent"_s, u"2026-11-11 Rent, late"_s, u"2026-12-12 Rent"_s}));

    // Given a repeat of its own at the same time, the old rule is no matter.
    const ics::Group twice =
        ics::parse(calendar("BEGIN:VEVENT\r\nUID:t\r\nDTSTART;VALUE=DATE:20261005\r\n"
                            "RRULE:FREQ=MONTHLY;BYDAY=1MO,3MO\r\nSUMMARY:Twice\r\nEND:VEVENT\r\n"));
    const QDateTime monday(QDate(2026, 10, 5), QTime(0, 0), kBerlin);
    const Event first = ics::expand(twice, kHome, monday, monday.addDays(1), kBerlin).value(0);
    EventEdit edit;
    edit.start = monday.addDays(1);
    edit.end = monday.addDays(2);
    edit.recurrence = QStringList{u"RRULE:FREQ=WEEKLY;BYDAY=TU"_s};
    QString error;
    const auto weekly = ics::update(twice, first, edit, EditScope::AllEvents, {}, &error);
    QVERIFY2(weekly, qPrintable(error));
    QCOMPARE(ics::expand(weekly->group, kHome, monday, monday.addDays(14), kBerlin).size(), 2);
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

void TestIcs::theOrganizerIsTheUser()
{
    ics::Group events = ics::parse(calendar(R"(BEGIN:VEVENT
UID:plan
DTSTART:20261006T130000Z
DTEND:20261006T140000Z
SUMMARY:Planning
ATTENDEE;PARTSTAT=NEEDS-ACTION:mailto:alex@example.com
END:VEVENT
BEGIN:VEVENT
UID:solo
DTSTART:20261007T130000Z
DTEND:20261007T140000Z
SUMMARY:Focus
END:VEVENT
)"));
    ics::takeOrganizer(events, {}, u"me@example.com"_s);
    const auto byUid = [&events](const QString &uid) {
        return *std::ranges::find_if(events, [&uid](const auto &e) { return e->uid() == uid; });
    };
    QCOMPARE(byUid(u"plan"_s)->organizer().email(), u"me@example.com"_s);
    QCOMPARE(byUid(u"plan"_s)->attendees().size(), 2);
    // No guests, no organizer.
    QVERIFY(byUid(u"solo"_s)->organizer().email().isEmpty());
    // Someone else's invitation keeps its organizer.
    ics::Group theirs = ics::parse(calendar(R"(BEGIN:VEVENT
UID:theirs
DTSTART:20261006T130000Z
DTEND:20261006T140000Z
ORGANIZER:mailto:sam@example.com
ATTENDEE:mailto:me@example.com
END:VEVENT
)"));
    ics::takeOrganizer(theirs, {}, u"me@example.com"_s);
    QCOMPARE(theirs.first()->organizer().email(), u"sam@example.com"_s);
    // Guests it had already, as in a moved event, are not new ones to invite.
    ics::Group moved = ics::parse(calendar(R"(BEGIN:VEVENT
UID:old
DTSTART:20261006T130000Z
DTEND:20261006T140000Z
ATTENDEE:mailto:alex@example.com
END:VEVENT
)"));
    const ics::Group before = ics::parse(calendar(R"(BEGIN:VEVENT
UID:old
DTSTART:20261006T120000Z
DTEND:20261006T130000Z
ATTENDEE:mailto:alex@example.com
END:VEVENT
)"));
    ics::takeOrganizer(moved, before, u"me@example.com"_s);
    QVERIFY(moved.first()->organizer().email().isEmpty());
}

void TestIcs::eventsGoToGoogleWithTheirRepeats()
{
    const QList<ics::GoogleImport> imports = ics::toGoogle(ics::parse(calendar(R"(BEGIN:VEVENT
UID:standup
DTSTART;TZID=Europe/Berlin:20261005T090000
DTEND;TZID=Europe/Berlin:20261005T091500
RRULE:FREQ=DAILY;COUNT=5
EXDATE:20261006T070000Z
SUMMARY:Standup
LOCATION:Room 1
ATTENDEE:mailto:alex@example.com
END:VEVENT
BEGIN:VEVENT
UID:standup
RECURRENCE-ID;TZID=Europe/Berlin:20261007T090000
DTSTART;TZID=Europe/Berlin:20261007T100000
DTEND;TZID=Europe/Berlin:20261007T101500
SUMMARY:Standup, later
END:VEVENT
BEGIN:VEVENT
UID:standup
RECURRENCE-ID;TZID=Europe/Berlin:20261008T090000
DTSTART;TZID=Europe/Berlin:20261008T090000
DTEND;TZID=Europe/Berlin:20261008T091500
STATUS:CANCELLED
SUMMARY:Standup
END:VEVENT
BEGIN:VEVENT
UID:gym
DTSTART:20261005T060000Z
DTEND:20261005T070000Z
RRULE:FREQ=WEEKLY
SUMMARY:Gym
END:VEVENT
BEGIN:VEVENT
UID:walk
DTSTART:20261006T080000
DTEND:20261006T090000
SUMMARY:Walk
END:VEVENT
BEGIN:VEVENT
UID:trip
DTSTART;VALUE=DATE:20261010
DTEND;VALUE=DATE:20261012
SUMMARY:Trip
END:VEVENT
)")));
    QCOMPARE(imports.size(), 4);
    const auto byUid = [&imports](const QString &uid) {
        return std::ranges::find_if(imports,
                                    [&uid](const ics::GoogleImport &i) {
                                        return i.event[u"iCalUID"].toString() == uid;
                                    })
            ->event;
    };
    const QJsonObject series = byUid(u"standup"_s);
    QCOMPARE(series[u"start"][u"timeZone"].toString(), u"Europe/Berlin"_s);
    QCOMPARE(QDateTime::fromString(series[u"end"][u"dateTime"].toString(), Qt::ISODate).toUTC(),
             QDateTime(QDate(2026, 10, 5), QTime(7, 15), QTimeZone::UTC));
    QCOMPARE(series[u"location"].toString(), u"Room 1"_s);
    QCOMPARE(series[u"attendees"][0][u"email"].toString(), u"alex@example.com"_s);
    const QStringList lines = series[u"recurrence"].toVariant().toStringList();
    QVERIFY(lines.contains(u"RRULE:FREQ=DAILY;COUNT=5"_s));
    // Its own skipped day and the cancelled one; the moved one stays, to be
    // patched once the series is in, since Google takes no RECURRENCE-ID.
    QVERIFY(lines.contains(u"EXDATE:20261006T070000Z"_s));
    QVERIFY(!lines.contains(u"EXDATE:20261007T070000Z"_s));
    QVERIFY(lines.contains(u"EXDATE:20261008T070000Z"_s));
    const auto standup = std::ranges::find_if(imports, [](const ics::GoogleImport &i) {
        return i.event[u"iCalUID"].toString() == u"standup";
    });
    QCOMPARE(standup->changed.size(), 1);
    const ics::GoogleImport::Occurrence moved = standup->changed.first();
    QCOMPARE(moved.start.toUTC(), QDateTime(QDate(2026, 10, 7), QTime(7, 0), QTimeZone::UTC));
    QCOMPARE(moved.fields[u"summary"].toString(), u"Standup, later"_s);
    // It leaves the series' room behind, which a patch must say.
    QVERIFY(moved.fields.contains(u"location"_s));
    QCOMPARE(moved.fields[u"location"].toString(), QString());
    QCOMPARE(moved.fields[u"transparency"].toString(), u"opaque"_s);
    QVERIFY(!moved.fields.contains(u"iCalUID"_s));
    // Written in UTC, it says so, as Google needs to repeat it.
    QCOMPARE(byUid(u"gym"_s)[u"start"][u"timeZone"].toString(), u"UTC"_s);
    // With no zone at all, in this computer's.
    const QJsonObject walk = byUid(u"walk"_s);
    QCOMPARE(walk[u"start"][u"timeZone"].toString(),
             QString::fromUtf8(QTimeZone::systemTimeZoneId()));
    QCOMPARE(walk[u"start"][u"dateTime"].toString().left(19), u"2026-10-06T08:00:00"_s);
    // All day, with Google's end the day after the last.
    const QJsonObject trip = byUid(u"trip"_s);
    QCOMPARE(trip[u"start"][u"date"].toString(), u"2026-10-10"_s);
    QCOMPARE(trip[u"end"][u"date"].toString(), u"2026-10-12"_s);

    // An all-day series' occurrence is found by its day, even made timed.
    const QList<ics::GoogleImport> days = ics::toGoogle(ics::parse(calendar(R"(BEGIN:VEVENT
UID:off
DTSTART;VALUE=DATE:20261010
DTEND;VALUE=DATE:20261011
RRULE:FREQ=WEEKLY;COUNT=3
SUMMARY:Off
END:VEVENT
BEGIN:VEVENT
UID:off
RECURRENCE-ID;VALUE=DATE:20261017
DTSTART:20261017T090000Z
DTEND:20261017T120000Z
SUMMARY:Morning off
END:VEVENT
)")));
    QCOMPARE(days.first().changed.size(), 1);
    QVERIFY(days.first().changed.first().allDay);
    // Given hours, it clears its date, which Google would otherwise keep.
    const QJsonObject start = days.first().changed.first().fields[u"start"].toObject();
    QVERIFY(start[u"dateTime"].isString());
    QVERIFY(start.contains(u"date"_s) && start[u"date"].isNull());

    // A changed occurrence without its series goes as an event of its own.
    const QList<ics::GoogleImport> alone = ics::toGoogle(ics::parse(calendar(R"(BEGIN:VEVENT
UID:other
RECURRENCE-ID:20261007T070000Z
DTSTART:20261007T090000Z
DTEND:20261007T091500Z
SUMMARY:Moved
END:VEVENT
)")));
    QCOMPARE(alone.size(), 1);
    QCOMPARE(alone.first().event[u"iCalUID"].toString(), u"other-20261007T070000Z"_s);
}

void TestIcs::googleEventsBecomeICalendar()
{
    GoogleEvent series;
    series.id = u"standup"_s;
    series.status = u"confirmed"_s;
    series.summary = u"Standup"_s;
    series.start = {
        {}, QDateTime(QDate(2026, 10, 5), QTime(7, 0), QTimeZone::UTC), u"Europe/Berlin"_s};
    series.end = {
        {}, QDateTime(QDate(2026, 10, 5), QTime(7, 15), QTimeZone::UTC), u"Europe/Berlin"_s};
    series.recurrence = {u"RRULE:FREQ=DAILY;COUNT=5"_s,
                         u"EXDATE;TZID=Europe/Berlin:20261006T090000"_s};
    series.attendees = R"([{"email":"me@example.com","organizer":true,"self":true,)"
                       R"("responseStatus":"accepted"},)"
                       R"({"email":"alex@example.com","responseStatus":"tentative"}])";
    GoogleEvent moved;
    moved.id = u"standup_20261007T070000Z"_s;
    moved.status = u"confirmed"_s;
    moved.summary = u"Standup, later"_s;
    moved.recurringEventId = u"standup"_s;
    moved.originalStart = {
        {}, QDateTime(QDate(2026, 10, 7), QTime(7, 0), QTimeZone::UTC), u"Europe/Berlin"_s};
    moved.start = {
        {}, QDateTime(QDate(2026, 10, 7), QTime(9, 0), QTimeZone::UTC), u"Europe/Berlin"_s};
    moved.end = {
        {}, QDateTime(QDate(2026, 10, 7), QTime(9, 15), QTimeZone::UTC), u"Europe/Berlin"_s};
    GoogleEvent skipped;
    skipped.id = u"standup_20261008T070000Z"_s;
    skipped.status = u"cancelled"_s;
    skipped.recurringEventId = u"standup"_s;
    skipped.originalStart = {
        {}, QDateTime(QDate(2026, 10, 8), QTime(7, 0), QTimeZone::UTC), u"Europe/Berlin"_s};
    GoogleEvent trip;
    trip.id = u"trip"_s;
    trip.status = u"confirmed"_s;
    trip.summary = u"Trip"_s;
    trip.start = {QDate(2026, 10, 10), {}, {}};
    trip.end = {QDate(2026, 10, 12), {}, {}};
    GoogleEvent focus = trip;
    focus.id = u"focus"_s;
    focus.eventType = u"focusTime"_s;

    // Written out and read back, as an export would be.
    const ics::Group events =
        ics::parse(ics::write(ics::fromGoogle({series, moved, skipped, trip, focus})));
    QCOMPARE(events.size(), 3);
    const auto find = [&events](const QString &uid, bool exception) {
        return *std::ranges::find_if(events, [&](const auto &e) {
            return e->uid() == uid && e->hasRecurrenceId() == exception;
        });
    };
    const auto master = find(u"standup@google.com"_s, false);
    QCOMPARE(master->dtStart().timeZone().id(), QByteArray("Europe/Berlin"));
    QCOMPARE(master->dtStart().time(), QTime(9, 0));
    QCOMPARE(master->recurrence()->rRules().size(), 1);
    // Its own skipped day and the cancelled occurrence.
    QCOMPARE(master->recurrence()->exDateTimes().size(), 2);
    QCOMPARE(master->organizer().email(), u"me@example.com"_s);
    QCOMPARE(master->attendees().size(), 2);
    QCOMPARE(master->attendees().last().status(), KCalendarCore::Attendee::Tentative);
    const auto later = find(u"standup@google.com"_s, true);
    QCOMPARE(later->summary(), u"Standup, later"_s);
    QCOMPARE(later->recurrenceId().toUTC(),
             QDateTime(QDate(2026, 10, 7), QTime(7, 0), QTimeZone::UTC));
    // All day, ending on its last day.
    const auto away = find(u"trip@google.com"_s, false);
    QVERIFY(away->allDay());
    QCOMPARE(away->dtEnd().date(), QDate(2026, 10, 11));
}

QTEST_MAIN(TestIcs)
#include "tst_ics.moc"
