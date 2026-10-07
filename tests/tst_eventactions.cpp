#include "EventActions.h"

#include "callie/SampleSource.h"

#include <QSignalSpy>
#include <QTest>
#include <QUrlQuery>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

/// A sample event that is an invitation, as EventModel would hand it over.
QVariantMap invitation(SampleSource &source)
{
    const QDateTime from = QDateTime::currentDateTime();
    for (const Event &e :
         source.eventsBetween(from, from.addDays(7), QTimeZone::systemTimeZone())) {
        if (e.canRespond)
            return {{u"calendarId"_s, e.calendarId}, {u"eventId"_s, e.eventId},
                    {u"seriesId"_s, e.seriesId},     {u"summary"_s, e.summary},
                    {u"start"_s, e.start},           {u"end"_s, e.end},
                    {u"attendees"_s, e.attendees}};
    }
    return {};
}

QString answerOf(SampleSource &source, const QString &eventId)
{
    const QDateTime from = QDateTime::currentDateTime().addDays(-1);
    for (const Event &e :
         source.eventsBetween(from, from.addDays(9), QTimeZone::systemTimeZone())) {
        if (e.eventId == eventId)
            return e.responseStatus;
    }
    return u"(gone)"_s;
}

/// Refuses every change, as a calendar Google will not let us edit would.
class RefusingSource : public SampleSource
{
public:
    void respond(const Event &, const QString &, bool, Created done) override
    {
        done(u"Google could not save the answer: Forbidden"_s);
    }
    void deleteEvent(const Event &, bool, Created done) override
    {
        done(u"Google could not delete the event: Forbidden"_s);
    }
};

} // namespace

class TestEventActions : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void answerReachesTheSource();
    void removeTakesItAway();
    void moveGivesNewTimes();
    void changesBecomeAnEdit();
    void allDayKeepsItsDays();
    void editorTimesFollowTheZone();
    void copiesKeepTheirLengthAndDays();
    void duplicateLandsInAWritableCalendar();
    void editsReachTheWholeSeries();
    void mailGoesToTheOtherGuests();
    void failureIsReportedForItsEvent();
};

void TestEventActions::answerReachesTheSource()
{
    SampleSource source;
    EventActions actions;
    actions.setProperty("source", QVariant::fromValue(&source));
    const QVariantMap event = invitation(source);
    QVERIFY(!event.isEmpty());
    QSignalSpy responded(&actions, &EventActions::responded);

    actions.respond(event, u"tentative"_s, false);

    QCOMPARE(responded.size(), 1);
    QCOMPARE(responded.first().first().toString(), event.value(u"eventId"_s).toString());
    QVERIFY(actions.error().isEmpty());
    QCOMPARE(answerOf(source, event.value(u"eventId"_s).toString()), u"tentative"_s);
}

void TestEventActions::removeTakesItAway()
{
    SampleSource source;
    EventActions actions;
    actions.setProperty("source", QVariant::fromValue(&source));
    const QVariantMap event = invitation(source);
    QSignalSpy removed(&actions, &EventActions::removed);

    actions.remove(event, false);

    QCOMPARE(removed.size(), 1);
    QCOMPARE(answerOf(source, event.value(u"eventId"_s).toString()), u"(gone)"_s);
}

void TestEventActions::moveGivesNewTimes()
{
    SampleSource source;
    const QVariantMap event = invitation(source);
    QVERIFY(!event.isEmpty());
    const QDateTime start = event.value(u"start"_s).toDateTime().addSecs(30 * 60);
    const QDateTime end = event.value(u"end"_s).toDateTime().addSecs(60 * 60);
    EventActions actions;
    actions.setProperty("source", QVariant::fromValue<CalendarSource *>(&source));
    QSignalSpy moved(&actions, &EventActions::moved);

    actions.move(event, start, end, false);

    QCOMPARE(moved.size(), 1);
    const QString id = event.value(u"eventId"_s).toString();
    QCOMPARE(moved.first().first().toString(), id);
    const QList<Event> events =
        source.eventsBetween(start.addDays(-1), start.addDays(1), QTimeZone::systemTimeZone());
    const auto found = std::find_if(events.cbegin(), events.cend(),
                                    [&id](const Event &e) { return e.eventId == id; });
    QVERIFY(found != events.cend());
    QCOMPARE(found->start, start);
    QCOMPARE(found->end, end);
}

void TestEventActions::mailGoesToTheOtherGuests()
{
    const QUrl url = EventActions::mailGuests(
        {{u"summary"_s, u"Plan & ship"_s},
         {u"attendees"_s, QStringList{u"pat@example.com"_s, u"lee@example.com"_s}}});
    QCOMPARE(url.scheme(), u"mailto"_s);
    QCOMPARE(url.path(), u"pat@example.com,lee@example.com"_s);
    QCOMPARE(QUrlQuery(url).queryItemValue(u"subject"_s, QUrl::FullyDecoded), u"Plan & ship"_s);

    QVERIFY(EventActions::mailGuests({{u"summary"_s, u"Alone"_s}}).isEmpty());
}

void TestEventActions::failureIsReportedForItsEvent()
{
    RefusingSource source;
    EventActions actions;
    actions.setProperty("source", QVariant::fromValue(static_cast<CalendarSource *>(&source)));
    const QVariantMap event = invitation(source);
    QSignalSpy responded(&actions, &EventActions::responded);
    QSignalSpy removed(&actions, &EventActions::removed);

    actions.respond(event, u"accepted"_s, false);
    QVERIFY(!actions.busy());
    QVERIFY(actions.error().contains(u"Forbidden"_s));
    QCOMPARE(actions.errorEventId(), event.value(u"eventId"_s).toString());
    QVERIFY(responded.isEmpty());

    actions.remove(event, false);
    QVERIFY(actions.error().contains(u"delete"_s));
    QVERIFY(removed.isEmpty());

    actions.clearError();
    QVERIFY(actions.error().isEmpty());
    QVERIFY(actions.errorEventId().isEmpty());
}

void TestEventActions::changesBecomeAnEdit()
{
    const QDateTime start(QDate(2026, 10, 8), QTime(15, 0), QTimeZone::UTC);
    const EventEdit edit = EventActions::toEdit({{u"summary"_s, u"  Review  "_s},
                                                 {u"start"_s, start},
                                                 {u"zone"_s, u"Europe/Berlin"_s},
                                                 {u"guests"_s, QStringList{u" a@x.com"_s, u""_s}},
                                                 {u"videoCall"_s, false}});
    QCOMPARE(*edit.summary, u"Review"_s);
    // The same moment, written in the event's own zone.
    QCOMPARE(*edit.start, start);
    QCOMPARE(edit.start->timeZone(), QTimeZone("Europe/Berlin"));
    QCOMPARE(*edit.guests, QStringList{u"a@x.com"_s});
    QVERIFY(!*edit.videoCall);
    QVERIFY(!edit.location && !edit.end && !edit.recurrence);
}

void TestEventActions::allDayKeepsItsDays()
{
    // Midnights in a zone hours ahead of the event's still mean those days.
    const QTimeZone tokyo("Asia/Tokyo");
    const EventEdit edit =
        EventActions::toEdit({{u"start"_s, QDateTime(QDate(2026, 10, 8), QTime(0, 0), tokyo)},
                              {u"end"_s, QDateTime(QDate(2026, 10, 9), QTime(0, 0), tokyo)},
                              {u"allDay"_s, true},
                              {u"zone"_s, u"America/New_York"_s}});
    QCOMPARE(edit.start->date(), QDate(2026, 10, 8));
    QCOMPARE(edit.end->date(), QDate(2026, 10, 9));
    QCOMPARE(edit.start->time(), QTime(0, 0));
}

void TestEventActions::editorTimesFollowTheZone()
{
    // New York springs forward on 8 March 2026.
    const QString zone = u"America/New_York"_s;
    const QDateTime day = QDate(2026, 3, 8).startOfDay();
    const QDateTime ten = EventActions::at(day, 10 * 60, zone);
    QCOMPARE(ten.toTimeZone(QTimeZone(zone.toUtf8())).time(), QTime(10, 0));
    QCOMPARE(ten.toUTC().time(), QTime(14, 0));
    QCOMPARE(EventActions::minutesOf(ten, zone), 10 * 60);
    QCOMPARE(EventActions::dayOf(ten, zone).date(), QDate(2026, 3, 8));
    // 23:30 in New York is the next day in UTC.
    const QDateTime late = EventActions::at(day, 23 * 60 + 30, zone);
    QCOMPARE(EventActions::dayOf(late, zone).date(), QDate(2026, 3, 8));
    QCOMPARE(EventActions::dayOf(late, u"UTC"_s).date(), QDate(2026, 3, 9));

    QCOMPARE(EventActions::daysBetween(day, EventActions::addDays(day, 3)), 3);
    QCOMPARE(EventActions::addDays(day, 1).time(), QTime(0, 0));
}

void TestEventActions::copiesKeepTheirLengthAndDays()
{
    const QDateTime start(QDate(2026, 10, 7), QTime(12, 0), QTimeZone::UTC);
    const QVariantMap lunch{{u"summary"_s, u"Lunch"_s},
                            {u"description"_s, u"Bring the menu"_s},
                            {u"start"_s, start},
                            {u"end"_s, start.addSecs(90 * 60)}};
    // Unplaced, the copy is where the event is.
    EventDraft same = EventActions::toCopy(lunch, {});
    QCOMPARE(same.start, start);
    QCOMPARE(same.description, u"Bring the menu"_s);
    // Placed, it keeps its length.
    const QDateTime friday(QDate(2026, 10, 9), QTime(15, 30), QTimeZone::UTC);
    EventDraft moved = EventActions::toCopy(lunch, friday);
    QCOMPARE(moved.start, friday);
    QCOMPARE(moved.end, friday.addSecs(90 * 60));

    // An all-day copy takes whole days from the day it is put on.
    const QDateTime monday = QDate(2026, 10, 5).startOfDay();
    const EventDraft trip = EventActions::toCopy({{u"summary"_s, u"Trip"_s},
                                                  {u"allDay"_s, true},
                                                  {u"start"_s, monday},
                                                  {u"end"_s, monday.addDays(2)}},
                                                 friday);
    QCOMPARE(trip.start.date(), QDate(2026, 10, 9));
    QCOMPARE(trip.start.time(), QTime(0, 0));
    QCOMPARE(trip.end.date(), QDate(2026, 10, 11));
}

void TestEventActions::duplicateLandsInAWritableCalendar()
{
    SampleSource source;
    EventActions actions;
    actions.setProperty("source", QVariant::fromValue<CalendarSource *>(&source));
    QVariantMap event = invitation(source);
    QVERIFY(!event.isEmpty());
    // Not a calendar the user can write to, so the copy goes elsewhere.
    event.insert(u"calendarId"_s, u"holidays"_s);
    QSignalSpy duplicated(&actions, &EventActions::duplicated);
    const QDateTime at = event.value(u"start"_s).toDateTime().addDays(1);
    actions.duplicate(event, at, u"nope"_s);
    QCOMPARE(duplicated.size(), 1);

    int copies = 0;
    for (const Event &e : source.eventsBetween(at.addSecs(-1), at.addSecs(1), QTimeZone::UTC)) {
        // The sample's own events can repeat at that time too.
        if (e.summary == event.value(u"summary"_s).toString() && e.start == at &&
            !e.uid.startsWith(u"sample-"_s)) {
            ++copies;
            QVERIFY(std::any_of(
                source.calendars().cbegin(), source.calendars().cend(),
                [&e](const CalendarInfo &c) { return c.id == e.calendarId && c.writable; }));
        }
    }
    QCOMPARE(copies, 1);
}

void TestEventActions::editsReachTheWholeSeries()
{
    SampleSource source;
    EventActions actions;
    actions.setProperty("source", QVariant::fromValue<CalendarSource *>(&source));
    const QVariantMap standup = [&source] {
        const QDateTime monday(QDate(2026, 10, 5), QTime(0, 0), QTimeZone::UTC);
        for (const Event &e : source.eventsBetween(monday, monday.addDays(1), QTimeZone::UTC)) {
            if (e.summary == u"Standup")
                return QVariantMap{{u"uid"_s, e.uid},
                                   {u"eventId"_s, e.eventId},
                                   {u"seriesId"_s, e.seriesId},
                                   {u"calendarId"_s, e.calendarId},
                                   {u"summary"_s, e.summary},
                                   {u"start"_s, e.start},
                                   {u"end"_s, e.end}};
        }
        return QVariantMap{};
    }();
    QSignalSpy updated(&actions, &EventActions::updated);
    actions.update(standup, {{u"summary"_s, u"Daily"_s}}, u"all"_s);
    QCOMPARE(updated.size(), 1);

    const QDateTime week(QDate(2026, 10, 5), QTime(0, 0), QTimeZone::UTC);
    int daily = 0;
    for (const Event &e : source.eventsBetween(week, week.addDays(7), QTimeZone::UTC)) {
        QVERIFY(e.summary != u"Standup");
        daily += e.summary == u"Daily";
    }
    QCOMPARE(daily, 5);
}

QTEST_GUILESS_MAIN(TestEventActions)
#include "tst_eventactions.moc"
