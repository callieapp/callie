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

QTEST_GUILESS_MAIN(TestEventActions)
#include "tst_eventactions.moc"
