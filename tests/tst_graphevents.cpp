#include "callie/GraphEvents.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(text).object();
}

/// Each event kind as it goes to Graph and back, for repeats Outlook keeps.
QString roundTrip(const QByteArray &rule, const char *start = "20261005T090000")
{
    const ics::Group events = ics::parse(
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//t//EN\r\nBEGIN:VEVENT\r\nUID:x\r\n"
        "DTSTART;TZID=Europe/Berlin:" +
        QByteArray(start) + "\r\nDTEND;TZID=Europe/Berlin:" + QByteArray(start).left(9) +
        "100000\r\nRRULE:" + rule + "\r\nSUMMARY:Repeat\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
    QString error;
    const auto fields = graph::fieldsFor(*events.first(), &error);
    if (!fields)
        return u"refused: "_s + error;
    return graph::ruleFor((*fields)[u"recurrence"].toObject(), events.first()->dtStart(), false);
}

} // namespace

class TestGraphEvents : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void zonesAreReadByWindowsOrIanaName();
    void aSeriesWithItsOccurrencesBecomesICalendar();
    void allDayEventsKeepTheirDays();
    void repeatsGoToOutlookAndBack();
    void repeatsOutlookCannotHoldAreRefused();
    void fieldsAreWhatGraphTakes();
};

void TestGraphEvents::zonesAreReadByWindowsOrIanaName()
{
    QCOMPARE(graph::zoneFor(u"Pacific Standard Time"_s).id(), QByteArray("America/Los_Angeles"));
    QCOMPARE(graph::zoneFor(u"Europe/Berlin"_s).id(), QByteArray("Europe/Berlin"));
    QCOMPARE(graph::zoneFor(u"tzone://Microsoft/Utc"_s), QTimeZone(QTimeZone::UTC));
    QCOMPARE(graph::zoneFor(u"Nowhere Standard Time"_s), QTimeZone(QTimeZone::UTC));
    QCOMPARE(graph::timeOf(json(R"({"dateTime":"2026-10-05T07:00:00.0000000","timeZone":"UTC"})")),
             QDateTime(QDate(2026, 10, 5), QTime(7, 0), QTimeZone::UTC));
}

void TestGraphEvents::aSeriesWithItsOccurrencesBecomesICalendar()
{
    graph::Series series;
    series.event = json(R"({"id":"m1","iCalUId":"040000-standup","subject":"Standup",
        "body":{"contentType":"text","content":"Notes\r\n"},
        "location":{"displayName":"Room 1"},
        "start":{"dateTime":"2026-10-05T07:00:00.0000000","timeZone":"UTC"},
        "end":{"dateTime":"2026-10-05T07:15:00.0000000","timeZone":"UTC"},
        "originalStartTimeZone":"W. Europe Standard Time","isAllDay":false,"showAs":"busy",
        "organizer":{"emailAddress":{"name":"Sam","address":"sam@contoso.com"}},
        "attendees":[{"emailAddress":{"name":"Me","address":"me@contoso.com"},
                      "status":{"response":"tentativelyAccepted"}}],
        "onlineMeeting":{"joinUrl":"https://teams.microsoft.com/l/meetup-join/1"},
        "recurrence":{"pattern":{"type":"daily","interval":1},
                      "range":{"type":"numbered","numberOfOccurrences":5,"startDate":"2026-10-05",
                               "recurrenceTimeZone":"W. Europe Standard Time"}}})");
    series.cancelled = {u"OID.m1.2026-10-08"_s};
    series.exceptions = {json(R"({"id":"e1","subject":"Standup, later","type":"exception",
        "originalStart":"2026-10-07T07:00:00Z",
        "start":{"dateTime":"2026-10-07T09:00:00.0000000","timeZone":"UTC"},
        "end":{"dateTime":"2026-10-07T09:15:00.0000000","timeZone":"UTC"}})")};

    // Written out and read back, as the copy on disk is.
    const ics::Group events = ics::parse(ics::write(graph::toIcs(series)));
    QCOMPARE(events.size(), 2);
    const auto master =
        *std::ranges::find_if(events, [](const auto &e) { return !e->hasRecurrenceId(); });
    QCOMPARE(master->uid(), u"040000-standup"_s);
    QCOMPARE(master->dtStart().timeZone().id(), QByteArray("Europe/Berlin"));
    QCOMPARE(master->dtStart().time(), QTime(9, 0));
    QCOMPARE(master->description(), u"Notes"_s);
    QCOMPARE(master->location(), u"Room 1"_s);
    QCOMPARE(master->recurrence()->duration(), 5);
    QCOMPARE(master->recurrence()->exDateTimes().size(), 1);
    QCOMPARE(master->recurrence()->exDateTimes().first().date(), QDate(2026, 10, 8));
    QCOMPARE(master->organizer().email(), u"sam@contoso.com"_s);
    QCOMPARE(master->attendees().first().status(), KCalendarCore::Attendee::Tentative);
    QCOMPARE(master->conferences().first().uri(),
             QUrl(u"https://teams.microsoft.com/l/meetup-join/1"_s));
    const auto later =
        *std::ranges::find_if(events, [](const auto &e) { return e->hasRecurrenceId(); });
    QCOMPARE(later->uid(), u"040000-standup"_s);
    QCOMPARE(later->recurrenceId().toUTC(),
             QDateTime(QDate(2026, 10, 7), QTime(7, 0), QTimeZone::UTC));
    QCOMPARE(later->dtStart().time(), QTime(11, 0));
}

void TestGraphEvents::allDayEventsKeepTheirDays()
{
    graph::Series trip;
    trip.event = json(R"({"id":"t","iCalUId":"trip","subject":"Trip","isAllDay":true,
        "showAs":"free","originalStartTimeZone":"Pacific Standard Time",
        "start":{"dateTime":"2026-10-10T00:00:00.0000000","timeZone":"UTC"},
        "end":{"dateTime":"2026-10-12T00:00:00.0000000","timeZone":"UTC"}})");
    const auto event = graph::toIcs(trip).first();
    QVERIFY(event->allDay());
    QCOMPARE(event->dtStart().date(), QDate(2026, 10, 10));
    QCOMPARE(event->dtEnd().date(), QDate(2026, 10, 11));
    QCOMPARE(event->transparency(), KCalendarCore::Event::Transparent);

    QString error;
    const auto fields = graph::fieldsFor(*event, &error);
    QVERIFY(fields);
    QCOMPARE((*fields)[u"start"][u"dateTime"].toString(), u"2026-10-10T00:00:00"_s);
    QCOMPARE((*fields)[u"end"][u"dateTime"].toString(), u"2026-10-12T00:00:00"_s);
    QCOMPARE((*fields)[u"showAs"].toString(), u"free"_s);
}

void TestGraphEvents::repeatsGoToOutlookAndBack()
{
    QCOMPARE(roundTrip("FREQ=DAILY;INTERVAL=2;COUNT=10"),
             u"RRULE:FREQ=DAILY;INTERVAL=2;COUNT=10"_s);
    QCOMPARE(roundTrip("FREQ=WEEKLY;BYDAY=MO,WE"), u"RRULE:FREQ=WEEKLY;BYDAY=MO,WE;WKST=MO"_s);
    QCOMPARE(roundTrip("FREQ=MONTHLY;BYMONTHDAY=5"), u"RRULE:FREQ=MONTHLY;BYMONTHDAY=5"_s);
    QCOMPARE(roundTrip("FREQ=MONTHLY;BYDAY=1MO"), u"RRULE:FREQ=MONTHLY;BYDAY=1MO"_s);
    QCOMPARE(roundTrip("FREQ=MONTHLY;BYDAY=-1FR", "20261030T090000"),
             u"RRULE:FREQ=MONTHLY;BYDAY=-1FR"_s);
    QCOMPARE(roundTrip("FREQ=YEARLY;BYMONTH=10;BYMONTHDAY=5"),
             u"RRULE:FREQ=YEARLY;BYMONTH=10;BYMONTHDAY=5"_s);
    QCOMPARE(roundTrip("FREQ=YEARLY;BYMONTH=10;BYDAY=1MO"),
             u"RRULE:FREQ=YEARLY;BYMONTH=10;BYDAY=1MO"_s);
    // Until a day, kept as the last day it may happen on.
    QCOMPARE(roundTrip("FREQ=DAILY;UNTIL=20261009T070000Z"),
             u"RRULE:FREQ=DAILY;UNTIL=20261009T070000Z"_s);
}

void TestGraphEvents::repeatsOutlookCannotHoldAreRefused()
{
    QVERIFY(roundTrip("FREQ=HOURLY").startsWith(u"refused"_s));
    QVERIFY(roundTrip("FREQ=MONTHLY;BYMONTHDAY=1,15").startsWith(u"refused"_s));
    QVERIFY(roundTrip("FREQ=MONTHLY;BYDAY=1MO,3WE").startsWith(u"refused"_s));
    QVERIFY(roundTrip("FREQ=YEARLY;BYMONTH=1,7").startsWith(u"refused"_s));
}

void TestGraphEvents::fieldsAreWhatGraphTakes()
{
    const ics::Group events = ics::parse(R"(BEGIN:VCALENDAR
VERSION:2.0
PRODID:-//t//EN
BEGIN:VEVENT
UID:plan
DTSTART;TZID=America/New_York:20261006T140000
DTEND;TZID=America/New_York:20261006T150000
SUMMARY:Planning
DESCRIPTION:Bring notes
LOCATION:Room 2
ORGANIZER:mailto:me@contoso.com
ATTENDEE;PARTSTAT=ACCEPTED:mailto:me@contoso.com
ATTENDEE;CN=Alex:mailto:alex@contoso.com
END:VEVENT
END:VCALENDAR
)");
    QString error;
    const auto fields = graph::fieldsFor(*events.first(), &error);
    QVERIFY(fields);
    QCOMPARE((*fields)[u"subject"].toString(), u"Planning"_s);
    QCOMPARE((*fields)[u"body"][u"content"].toString(), u"Bring notes"_s);
    QCOMPARE((*fields)[u"location"][u"displayName"].toString(), u"Room 2"_s);
    QCOMPARE((*fields)[u"start"][u"dateTime"].toString(), u"2026-10-06T14:00:00"_s);
    QCOMPARE((*fields)[u"start"][u"timeZone"].toString(), u"Eastern Standard Time"_s);
    // The organizer is not one of its own guests.
    const QJsonArray attendees = (*fields)[u"attendees"].toArray();
    QCOMPARE(attendees.size(), 1);
    QCOMPARE(attendees.first()[u"emailAddress"][u"address"].toString(), u"alex@contoso.com"_s);
    QVERIFY(!fields->contains(u"recurrence"_s));
}

QTEST_GUILESS_MAIN(TestGraphEvents)
#include "tst_graphevents.moc"
