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

} // namespace

class TestEventActions : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void answerReachesTheSource();
    void removeTakesItAway();
    void mailGoesToTheOtherGuests();
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

QTEST_GUILESS_MAIN(TestEventActions)
#include "tst_eventactions.moc"
