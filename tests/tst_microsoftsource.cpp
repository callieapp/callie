#include "FakeGraphServer.h"
#include "FakeTokenStore.h"

#include "callie/MicrosoftSource.h"
#include "callie/MicrosoftTokenProvider.h"

#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestMicrosoftSource : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void firstSyncBringsCalendarsDown();
    void changesGoUpAndComeDown();
    void occurrencesAreChangedOnTheirOwn();
    void answersGoAsAnswers();
    void teamsLinksAreAskedFor();
    void changesWaitWhileOutlookIsDown();
    void removingAnAccountForgetsItsCopies();

private:
    void sync();
    [[nodiscard]] QList<Event> week() const;
    [[nodiscard]] Event find(const QString &summary, int day = 0) const;
    [[nodiscard]] bool called(const QByteArray &method, const QString &path) const;

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<FakeGraphServer> m_graph;
    std::unique_ptr<FakeHttpServer> m_tokenServer;
    std::unique_ptr<QNetworkAccessManager> m_network;
    std::unique_ptr<FakeTokenStore> m_store;
    std::unique_ptr<MicrosoftTokenProvider> m_tokens;
    std::unique_ptr<GraphApi> m_api;
    std::unique_ptr<MicrosoftSource> m_source;
    QString m_lunch;
    QString m_standup;
    QString m_review;
};

namespace {

const QString kMe = u"me@contoso.com"_s;
const QDateTime kMonday(QDate(2026, 10, 5), QTime(0, 0), QTimeZone::UTC);

QJsonObject at(const char *dateTime)
{
    return {{u"dateTime"_s, QString::fromLatin1(dateTime) + u".0000000"_s},
            {u"timeZone"_s, u"UTC"_s}};
}

QString outcome(const std::function<void(CalendarSource::Created)> &call)
{
    QString error = u"no answer"_s;
    call([&error](const Outcome &result) { error = result.error; });
    return error;
}

} // namespace

void TestMicrosoftSource::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_graph = std::make_unique<FakeGraphServer>();
    m_graph->addCalendar(u"cal1"_s, {u"Work"_s});
    m_lunch = m_graph->add(u"cal1"_s, {{u"subject"_s, u"Lunch"_s},
                                       {u"start"_s, at("2026-10-06T12:00:00")},
                                       {u"end"_s, at("2026-10-06T13:00:00")}});
    m_standup = m_graph->add(
        u"cal1"_s,
        {{u"subject"_s, u"Standup"_s},
         {u"start"_s, at("2026-10-05T07:00:00")},
         {u"end"_s, at("2026-10-05T07:15:00")},
         {u"originalStartTimeZone"_s, u"UTC"_s},
         {u"recurrence"_s,
          QJsonObject{{u"pattern"_s, QJsonObject{{u"type"_s, u"daily"_s}, {u"interval"_s, 1}}},
                      {u"range"_s, QJsonObject{{u"type"_s, u"numbered"_s},
                                               {u"numberOfOccurrences"_s, 5},
                                               {u"startDate"_s, u"2026-10-05"_s}}}}}});
    m_review = m_graph->add(
        u"cal1"_s,
        {{u"subject"_s, u"Review"_s},
         {u"start"_s, at("2026-10-08T15:00:00")},
         {u"end"_s, at("2026-10-08T16:00:00")},
         {u"isOrganizer"_s, false},
         {u"organizer"_s,
          QJsonObject{{u"emailAddress"_s, QJsonObject{{u"address"_s, u"sam@contoso.com"_s}}}}},
         {u"attendees"_s, QJsonArray{QJsonObject{
                              {u"emailAddress"_s, QJsonObject{{u"address"_s, kMe}}},
                              {u"status"_s, QJsonObject{{u"response"_s, u"notResponded"_s}}}}}}});
    m_graph->people = {
        QJsonObject{{u"displayName"_s, u"Alex"_s},
                    {u"scoredEmailAddresses"_s,
                     QJsonArray{QJsonObject{{u"address"_s, u"alex@contoso.com"_s}}}}}};
    m_tokenServer = std::make_unique<FakeHttpServer>();
    m_tokenServer->respond(200, R"({"access_token":"at","refresh_token":"rt","expires_in":3600})");
    m_network = std::make_unique<QNetworkAccessManager>();
    m_store = std::make_unique<FakeTokenStore>();
    m_store->secrets.insert(kMe, u"rt"_s);
    m_tokens = std::make_unique<MicrosoftTokenProvider>(MicrosoftClientConfig{u"app"_s}, *m_store);
    m_tokens->setTokenUrl(m_tokenServer->url(u"/token"_s));
    m_api = std::make_unique<GraphApi>(m_network.get());
    m_api->setBaseUrl(m_graph->url());
    m_source = std::make_unique<MicrosoftSource>(m_dir->path(), m_api.get(), m_tokens.get());
    m_source->setAccounts({kMe});
}

void TestMicrosoftSource::sync()
{
    QSignalSpy synced(m_source.get(), &MicrosoftSource::synced);
    m_source->refresh();
    QVERIFY(synced.wait(5000));
}

QList<Event> TestMicrosoftSource::week() const
{
    return m_source->eventsBetween(kMonday, kMonday.addDays(7), QTimeZone::UTC);
}

Event TestMicrosoftSource::find(const QString &summary, int day) const
{
    for (const Event &event : week()) {
        if (event.summary == summary && (day == 0 || event.start.date().day() == day))
            return event;
    }
    return {};
}

bool TestMicrosoftSource::called(const QByteArray &method, const QString &path) const
{
    return std::ranges::any_of(m_graph->calls, [&](const FakeGraphServer::Call &call) {
        return call.method == method && call.path == path;
    });
}

void TestMicrosoftSource::firstSyncBringsCalendarsDown()
{
    QSignalSpy contacts(m_source.get(), &MicrosoftSource::contactsFound);
    // A changed and a cancelled occurrence, made elsewhere.
    FakeGraphServer::Item &standup = m_graph->items[m_standup];
    QJsonObject later = standup.event;
    later.insert(u"start"_s, at("2026-10-07T09:00:00"));
    later.insert(u"end"_s, at("2026-10-07T09:15:00"));
    later.insert(u"originalStart"_s, u"2026-10-07T07:00:00Z"_s);
    standup.exceptions.insert(u"2026-10-07T07:00:00Z"_s, later);
    standup.cancelled << u"2026-10-08"_s;

    sync();
    QVERIFY2(m_source->lastError().isEmpty(), qPrintable(m_source->lastError()));
    const QList<CalendarInfo> calendars = m_source->calendars();
    QCOMPARE(calendars.size(), 1);
    QCOMPARE(calendars.first().displayName, u"Work"_s);
    QCOMPARE(calendars.first().account, kMe);
    QCOMPARE(calendars.first().color, QColor(u"#E86AA6"_s));
    QVERIFY(calendars.first().videoCalls);
    QVERIFY(calendars.first().invites);

    QVERIFY(!find(u"Lunch"_s).uid.isEmpty());
    int standups = 0;
    for (const Event &event : week())
        standups += event.summary == u"Standup";
    QCOMPARE(standups, 4);
    QCOMPARE(find(u"Standup"_s, 7).start.time(), QTime(9, 0));
    QCOMPARE(find(u"Review"_s).responseStatus, u"needsAction"_s);
    QTRY_COMPARE(contacts.size(), 1);
    QCOMPARE(contacts.first().at(1).value<QList<Contact>>().first().email, u"alex@contoso.com"_s);
}

void TestMicrosoftSource::changesGoUpAndComeDown()
{
    sync();
    const QString work = m_source->calendars().first().id;

    // Made here, it lands in Outlook.
    EventDraft draft;
    draft.calendarId = work;
    draft.summary = u"Planning"_s;
    draft.start = kMonday.addDays(2).addSecs(10 * 3600);
    draft.end = draft.start.addSecs(3600);
    QVERIFY(outcome([&](auto done) { m_source->createEvent(draft, done); }).isEmpty());
    QVERIFY(!m_source->waitingChanges().isEmpty());
    sync();
    QVERIFY(m_source->waitingChanges().isEmpty());
    QVERIFY(std::ranges::any_of(m_graph->items, [](const FakeGraphServer::Item &item) {
        return item.event[u"subject"].toString() == u"Planning";
    }));

    // Moved here, Outlook's copy moves; deleted here, it goes.
    const Event lunch = find(u"Lunch"_s);
    QVERIFY(outcome([&](auto done) {
                m_source->moveEvent(lunch, lunch.start.addSecs(3600), lunch.end.addSecs(3600),
                                    false, done);
            }).isEmpty());
    sync();
    QVERIFY(called("PATCH", u"me/events/"_s + m_lunch));
    QCOMPARE(m_graph->items[m_lunch].event[u"start"][u"dateTime"].toString(),
             u"2026-10-06T13:00:00"_s);
    QCOMPARE(find(u"Lunch"_s).start.time(), QTime(13, 0));
    QVERIFY(outcome([&](auto done) {
                m_source->deleteEvent(find(u"Lunch"_s), false, done);
            }).isEmpty());
    sync();
    QVERIFY(!m_graph->items.contains(m_lunch));
    QVERIFY(find(u"Lunch"_s).uid.isEmpty());

    // Changed elsewhere, it comes down.
    m_graph->change(m_review, {{u"subject"_s, u"Design review"_s}});
    sync();
    QVERIFY(!find(u"Design review"_s).uid.isEmpty());
}

void TestMicrosoftSource::occurrencesAreChangedOnTheirOwn()
{
    sync();
    const Event friday = find(u"Standup"_s, 9);
    QVERIFY(outcome([&](auto done) {
                m_source->moveEvent(friday, friday.start.addSecs(3600), friday.end.addSecs(3600),
                                    false, done);
            }).isEmpty());
    sync();
    QVERIFY(called("PATCH", u"me/events/"_s + m_standup + u"_20261009T070000"_s));
    QVERIFY(m_graph->items[m_standup].exceptions.contains(u"2026-10-09T07:00:00Z"_s));
    // The series itself is left alone.
    QVERIFY(!called("PATCH", u"me/events/"_s + m_standup));

    const Event tuesday = find(u"Standup"_s, 6);
    QVERIFY(outcome([&](auto done) { m_source->deleteEvent(tuesday, false, done); }).isEmpty());
    sync();
    QVERIFY(called("DELETE", u"me/events/"_s + m_standup + u"_20261006T070000"_s));
    QVERIFY(m_graph->items[m_standup].cancelled.contains(u"2026-10-06"_s));
    QVERIFY(find(u"Standup"_s, 6).uid.isEmpty());
    QCOMPARE(find(u"Standup"_s, 9).start.time(), QTime(8, 0));
}

void TestMicrosoftSource::answersGoAsAnswers()
{
    sync();
    QVERIFY(outcome([&](auto done) {
                m_source->respond(find(u"Review"_s), u"accepted"_s, false, done);
            }).isEmpty());
    sync();
    QVERIFY(called("POST", u"me/events/"_s + m_review + u"/accept"_s));
    // An invitation is the organizer's to change, so nothing else goes.
    QVERIFY(!called("PATCH", u"me/events/"_s + m_review));
    QVERIFY(m_source->waitingChanges().isEmpty());
}

void TestMicrosoftSource::teamsLinksAreAskedFor()
{
    sync();
    EventEdit call;
    call.videoCall = true;
    QVERIFY(outcome([&](auto done) {
                m_source->updateEvent(find(u"Lunch"_s), call, EditScope::ThisEvent, done);
            }).isEmpty());
    QVERIFY(!m_source->waitingChanges().isEmpty());
    sync();
    const auto patch = std::ranges::find_if(m_graph->calls, [this](const auto &c) {
        return c.method == "PATCH" && c.path == u"me/events/"_s + m_lunch;
    });
    QVERIFY(patch != m_graph->calls.cend());
    QCOMPARE(patch->body[u"onlineMeetingProvider"].toString(), u"teamsForBusiness"_s);
    QVERIFY(find(u"Lunch"_s).conferenceUrl.isValid());
    QVERIFY(m_source->waitingChanges().isEmpty());

    // Once there, Outlook keeps it.
    call.videoCall = false;
    QVERIFY(!outcome([&](auto done) {
                 m_source->updateEvent(find(u"Lunch"_s), call, EditScope::ThisEvent, done);
             }).isEmpty());
}

void TestMicrosoftSource::changesWaitWhileOutlookIsDown()
{
    sync();
    m_graph->down = 503;
    const Event lunch = find(u"Lunch"_s);
    QVERIFY(outcome([&](auto done) {
                m_source->moveEvent(lunch, lunch.start.addSecs(3600), lunch.end.addSecs(3600),
                                    false, done);
            }).isEmpty());
    sync();
    QVERIFY(!m_source->lastError().isEmpty());
    QVERIFY(!m_source->waitingChanges().isEmpty());
    QCOMPARE(find(u"Lunch"_s).start.time(), QTime(13, 0));

    m_graph->down = 0;
    sync();
    QVERIFY2(m_source->lastError().isEmpty(), qPrintable(m_source->lastError()));
    QVERIFY(m_source->waitingChanges().isEmpty());
    QCOMPARE(m_graph->items[m_lunch].event[u"start"][u"dateTime"].toString(),
             u"2026-10-06T13:00:00"_s);
}

void TestMicrosoftSource::removingAnAccountForgetsItsCopies()
{
    sync();
    QString error;
    QVERIFY(m_source->forget(kMe, &error));
    QVERIFY(m_source->calendars().isEmpty());
    QVERIFY(QDir(m_dir->path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
}

QTEST_GUILESS_MAIN(TestMicrosoftSource)
#include "tst_microsoftsource.moc"
