#include "FakeHttpServer.h"
#include "FakeTokenStore.h"

#include "callie/GoogleCache.h"
#include "callie/GoogleCalendarApi.h"
#include "callie/GoogleSource.h"
#include "callie/GoogleSync.h"
#include "callie/GoogleTokenProvider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

const Account kAccount{u"google"_s, u"me@example.com"_s};
const QTimeZone kNewYork("America/New_York");

GoogleEvent parsed(const char *json)
{
    return parseGoogleEvent(QJsonDocument::fromJson(json).object());
}

GoogleCalendar calendar(const QString &id, bool selected, const QString &role = u"owner"_s)
{
    GoogleCalendar result;
    result.id = id;
    result.summary = id.toUpper();
    result.color = u"#9fe1e7"_s;
    result.accessRole = role;
    result.selected = selected;
    return result;
}

} // namespace

class TestGoogleSource : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();

    void listsCalendarsWithColorAndAccess();
    void eventsComeFromSelectedCalendarsOnly();
    void eventsAreInTheRequestedZone();
    void seriesAreExpanded();
    void readOnlyCalendarsAllowNoChanges();
    void remindersFallBackToTheCalendar();
    void backgroundLoadMatchesDirectRead();
    void refreshWithoutSyncRereadsCache();
    void refreshSyncsAndReportsErrorsPerAccount();
    void refreshWhileSyncingStartsNothingNew();
    void removingTheLastAccountMidSyncLeavesNoSyncTime();
    void syncReportsOneChangePerBurst();
    void createdEventShowsWithoutASync();
    void createInUnknownCalendarFails();
    void deleteAimsAtTheOccurrenceOrTheSeries();
    void answerAimsAtTheTimedOccurrence();
    void actionsNeedAKnownCalendar();
    void moveShiftsTheOccurrenceOrTheSeries();
    void editPatchesOnlyWhatChanged();
    void editingTheSeriesKeepsItsFirstDay();
    void editingOnlyTheEndKeepsTheSeriesClock();
    void allDayClearsTheTime();
    void followingEditsSplitTheSeries();
    void splitStopsWhenTheNewSeriesFails();
    void splitGoesOnWhenTheNewSeriesExists();
    void splitKeepsWhatTheOccurrenceHad();
    void splitAllDaySeriesByDates();
    void seriesMoveKeepsTheWallClockAcrossDst();
    void statusStartsFromTheCache();
    void reportDescribesEachAccount();
    void accountsCanChangeWhileRunning();
    void statusFollowsARefresh();
    void calendarErrorShowsAfterRestart();
    void overlappingRefreshesReportEachErrorOnce();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<GoogleCache> m_cache;

    /// Renames Wednesday's standup and the ones after it, with `handler`
    /// answering Google's part, and gives back what Google was asked.
    QList<FakeHttpServer::Request> splitStandup(decltype(FakeHttpServer::handler) handler,
                                                QString &error);
};

void TestGoogleSource::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_cache = std::make_unique<GoogleCache>(m_dir->filePath(u"google.sqlite"_s));
    QVERIFY(m_cache->open());
    QVERIFY(m_cache->setCalendars(
        kAccount, {calendar(u"mine"_s, true), calendar(u"hidden"_s, false, u"reader"_s)}));
    QVERIFY(m_cache->applyChanges(kAccount, u"mine"_s,
                                  {{parsed(R"({"id":"lunch","summary":"Lunch",
                     "start":{"dateTime":"2026-10-06T12:00:00-04:00"},
                     "end":{"dateTime":"2026-10-06T13:00:00-04:00"}})"),
                                    parsed(R"({"id":"standup","summary":"Standup",
                     "start":{"dateTime":"2026-10-05T09:30:00-04:00",
                              "timeZone":"America/New_York"},
                     "end":{"dateTime":"2026-10-05T09:45:00-04:00",
                            "timeZone":"America/New_York"},
                     "recurrence":["RRULE:FREQ=DAILY;COUNT=5"]})")},
                                   u"t"_s},
                                  true));
    QVERIFY(m_cache->applyChanges(
        kAccount, u"hidden"_s,
        {{parsed(R"({"id":"secret","start":{"dateTime":"2026-10-06T15:00:00Z"},
                     "end":{"dateTime":"2026-10-06T16:00:00Z"}})")},
         u"t"_s},
        true));
}

void TestGoogleSource::listsCalendarsWithColorAndAccess()
{
    const GoogleSource source(*m_cache, {kAccount});
    const QList<CalendarInfo> calendars = source.calendars();

    QCOMPARE(calendars.size(), 2);
    QCOMPARE(calendars.at(0).id, u"me@example.com/mine"_s);
    QCOMPARE(calendars.at(0).displayName, u"MINE"_s);
    QCOMPARE(calendars.at(0).color, QColor(u"#9fe1e7"_s));
    QVERIFY(calendars.at(0).writable);
    QVERIFY(calendars.at(0).enabled);
    QVERIFY(!calendars.at(1).writable);
    QVERIFY(!calendars.at(1).enabled);
}

void TestGoogleSource::eventsComeFromSelectedCalendarsOnly()
{
    const GoogleSource source(*m_cache, {kAccount});
    const QList<Event> events =
        source.eventsBetween(QDateTime(QDate(2026, 10, 6), QTime(0, 0), kNewYork),
                             QDateTime(QDate(2026, 10, 7), QTime(0, 0), kNewYork), kNewYork);

    QStringList ids;
    for (const Event &e : events)
        ids.append(e.uid);
    ids.sort();
    QCOMPARE(ids, (QStringList{u"lunch"_s, u"standup"_s}));
    QCOMPARE(events.first().calendarId, u"me@example.com/mine"_s);
    QCOMPARE(events.first().color, QColor(u"#9fe1e7"_s));
}

void TestGoogleSource::eventsAreInTheRequestedZone()
{
    const QTimeZone tokyo("Asia/Tokyo");
    const GoogleSource source(*m_cache, {kAccount});
    const QList<Event> events =
        source.eventsBetween(QDateTime(QDate(2026, 10, 7), QTime(0, 0), tokyo),
                             QDateTime(QDate(2026, 10, 8), QTime(0, 0), tokyo), tokyo);

    // Lunch at 12:00 in New York is 01:00 the next day in Tokyo.
    const auto lunch = std::find_if(events.cbegin(), events.cend(),
                                    [](const Event &e) { return e.uid == u"lunch"; });
    QVERIFY(lunch != events.cend());
    QCOMPARE(lunch->start.timeZone(), tokyo);
    QCOMPARE(lunch->start.time(), QTime(1, 0));
}

void TestGoogleSource::seriesAreExpanded()
{
    const GoogleSource source(*m_cache, {kAccount});
    const QList<Event> events =
        source.eventsBetween(QDateTime(QDate(2026, 10, 1), QTime(0, 0), kNewYork),
                             QDateTime(QDate(2026, 10, 31), QTime(0, 0), kNewYork), kNewYork);

    const auto standups = std::count_if(events.cbegin(), events.cend(),
                                        [](const Event &e) { return e.uid == u"standup"; });
    QCOMPARE(standups, 5);
}

void TestGoogleSource::backgroundLoadMatchesDirectRead()
{
    const GoogleSource source(*m_cache, {kAccount});
    const QDateTime from(QDate(2026, 10, 1), QTime(0, 0), kNewYork);
    const QDateTime to(QDate(2026, 10, 31), QTime(0, 0), kNewYork);

    QFuture<SourceSnapshot> future = source.load(from, to, kNewYork);
    future.waitForFinished();
    const SourceSnapshot snapshot = future.result();

    const QList<Event> direct = source.eventsBetween(from, to, kNewYork);
    QVERIFY(!direct.isEmpty());
    QCOMPARE(snapshot.events.size(), direct.size());
    for (qsizetype i = 0; i < direct.size(); ++i) {
        QCOMPARE(snapshot.events.at(i).uid, direct.at(i).uid);
        QCOMPARE(snapshot.events.at(i).start, direct.at(i).start);
        QCOMPARE(snapshot.events.at(i).color, direct.at(i).color);
    }
    QCOMPARE(snapshot.calendars.size(), source.calendars().size());
    QCOMPARE(snapshot.calendars.first().id, source.calendars().first().id);
    QCOMPARE(snapshot.calendars.first().account, kAccount.id);
}

void TestGoogleSource::readOnlyCalendarsAllowNoChanges()
{
    QVERIFY(m_cache->setCalendars(kAccount, {calendar(u"mine"_s, true),
                                             calendar(u"hidden"_s, false, u"reader"_s),
                                             calendar(u"shared"_s, true, u"reader"_s)}));
    QVERIFY(
        m_cache->applyChanges(kAccount, u"shared"_s,
                              // Organized by the user, so only the reader calendar stops changes.
                              {{parsed(R"({"id":"talk","summary":"Talk","organizer":{"self":true},
                     "start":{"dateTime":"2026-10-06T15:00:00Z"},
                     "end":{"dateTime":"2026-10-06T16:00:00Z"},
                     "attendees":[{"email":"me@example.com","self":true},
                                  {"email":"pat@example.com"}]})"),
                                parsed(R"({"id":"ask","summary":"Ask",
                     "start":{"dateTime":"2026-10-06T17:00:00Z"},
                     "end":{"dateTime":"2026-10-06T18:00:00Z"},
                     "attendees":[{"email":"me@example.com","self":true}]})")},
                               u"t"_s},
                              true));
    const GoogleSource source(*m_cache, {kAccount});
    const QList<Event> events = source.eventsBetween(
        QDateTime(QDate(2026, 10, 6), QTime(0, 0), QTimeZone::UTC),
        QDateTime(QDate(2026, 10, 7), QTime(0, 0), QTimeZone::UTC), QTimeZone::UTC);

    QVERIFY(std::any_of(events.cbegin(), events.cend(),
                        [](const Event &e) { return e.summary == u"Talk"; }));
    for (const Event &event : events) {
        if (event.summary == u"Talk") {
            QVERIFY(!event.canEdit);
        } else if (event.summary == u"Ask") {
            QVERIFY(!event.canRespond);
        } else if (event.summary == u"Lunch") {
            QVERIFY(event.canEdit);
            QCOMPARE(event.eventId, u"lunch"_s);
        }
    }
}

void TestGoogleSource::remindersFallBackToTheCalendar()
{
    GoogleCalendar mine = calendar(u"mine"_s, true);
    mine.defaultReminders = {10};
    QVERIFY(m_cache->setCalendars(kAccount, {mine}));
    QVERIFY(m_cache->applyChanges(kAccount, u"mine"_s,
                                  {{parsed(R"({"id":"call","summary":"Call",
                     "reminders":{"useDefault":false,"overrides":[{"method":"popup","minutes":2}]},
                     "start":{"dateTime":"2026-10-06T15:00:00Z"},
                     "end":{"dateTime":"2026-10-06T16:00:00Z"}})"),
                                    parsed(R"({"id":"quiet","summary":"Quiet",
                     "reminders":{"useDefault":false},
                     "start":{"dateTime":"2026-10-06T17:00:00Z"},
                     "end":{"dateTime":"2026-10-06T18:00:00Z"}})")},
                                   u"t2"_s},
                                  false));
    const GoogleSource source(*m_cache, {kAccount});
    const QList<Event> events = source.eventsBetween(
        QDateTime(QDate(2026, 10, 6), QTime(0, 0), QTimeZone::UTC),
        QDateTime(QDate(2026, 10, 7), QTime(0, 0), QTimeZone::UTC), QTimeZone::UTC);

    QVERIFY(std::any_of(events.cbegin(), events.cend(),
                        [](const Event &e) { return e.summary == u"Call"; }));
    for (const Event &event : events) {
        QVERIFY(event.remindersKnown);
        if (event.summary == u"Call")
            QCOMPARE(event.reminders, QList<int>{2});
        else if (event.summary == u"Quiet")
            QVERIFY(event.reminders.isEmpty());
        else
            QCOMPARE(event.reminders, QList<int>{10});
    }
}

void TestGoogleSource::refreshWithoutSyncRereadsCache()
{
    GoogleSource source(*m_cache, {kAccount});
    QSignalSpy changed(&source, &CalendarSource::changed);

    source.refresh();

    QCOMPARE(changed.size(), 1);
}

namespace {

/// A GoogleSync against fake servers, for driving refresh().
struct SyncHarness
{
    explicit SyncHarness(GoogleCache &cache)
        : tokens(GoogleClientConfig{u"id"_s, u"secret"_s}, store), api(&network),
          sync(tokens, api, cache)
    {
        tokenServer.respond(200,
                            R"({"access_token":"at","expires_in":3600,"token_type":"Bearer"})");
        apiServer.handler = [](const FakeHttpServer::Request &request) {
            if (request.target.contains("calendarList"))
                return FakeHttpServer::Response(200,
                                                R"({"items":[{"id":"mine","selected":true}]})");
            return FakeHttpServer::Response(200, R"({"items":[],"nextSyncToken":"s"})");
        };
        tokens.setTokenUrl(tokenServer.url(u"/token"_s));
        api.setBaseUrl(apiServer.url(u"/v3/"_s));
    }

    FakeHttpServer tokenServer;
    FakeHttpServer apiServer;
    FakeTokenStore store;
    QNetworkAccessManager network;
    GoogleTokenProvider tokens;
    GoogleCalendarApi api;
    GoogleSync sync;
};

} // namespace

void TestGoogleSource::refreshSyncsAndReportsErrorsPerAccount()
{
    const Account other{u"google"_s, u"other@example.com"_s};
    SyncHarness harness(*m_cache);
    harness.store.secrets.insert(kAccount.id, u"rt"_s);
    GoogleSource source(*m_cache, {kAccount, other});
    source.setSync(&harness.sync);
    QSignalSpy errors(&source, &CalendarSource::errorOccurred);

    source.refresh();

    // The other account has no stored token, so only it fails.
    QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 5000);
    QVERIFY(errors.first().first().toString().startsWith(u"other@example.com: "_s));
    QTRY_COMPARE_WITH_TIMEOUT(harness.tokenServer.requests.size(), 1, 5000);
}

void TestGoogleSource::removingTheLastAccountMidSyncLeavesNoSyncTime()
{
    SyncHarness harness(*m_cache);
    harness.store.secrets.insert(kAccount.id, u"rt"_s);
    GoogleSource source(*m_cache, {kAccount});
    source.setSync(&harness.sync);

    source.refresh();
    QVERIFY(source.syncing());
    source.setAccounts({});

    QTRY_VERIFY_WITH_TIMEOUT(!source.syncing(), 5000);
    QVERIFY(!source.lastSynced().isValid());
    QVERIFY(source.lastError().isEmpty());
}

void TestGoogleSource::refreshWhileSyncingStartsNothingNew()
{
    SyncHarness harness(*m_cache);
    harness.store.secrets.insert(kAccount.id, u"rt"_s);
    GoogleSource source(*m_cache, {kAccount});
    source.setSync(&harness.sync);
    QSignalSpy changed(&source, &CalendarSource::changed);

    source.refresh();
    source.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!changed.isEmpty(), 5000);
    QTest::qWait(200);

    // One calendar list and one events request: the second refresh was ignored.
    // A first sync also reads the account's settings, which is not counted.
    QCOMPARE(harness.store.reads, 1);
    const auto calendarRequests = std::count_if(
        harness.apiServer.requests.cbegin(), harness.apiServer.requests.cend(),
        [](const FakeHttpServer::Request &r) { return !r.target.contains("/settings"); });
    QCOMPARE(calendarRequests, 2);
}

void TestGoogleSource::syncReportsOneChangePerBurst()
{
    SyncHarness harness(*m_cache);
    harness.store.secrets.insert(kAccount.id, u"rt"_s);
    harness.apiServer.handler = [](const FakeHttpServer::Request &request) {
        if (request.target.contains("calendarList"))
            return FakeHttpServer::Response(
                200, R"({"items":[{"id":"a","selected":true},{"id":"b","selected":true},
                                  {"id":"c","selected":true}]})");
        return FakeHttpServer::Response(200, R"({"items":[],"nextSyncToken":"s"})");
    };
    GoogleSource source(*m_cache, {kAccount});
    source.setSync(&harness.sync);
    QSignalSpy changed(&source, &CalendarSource::changed);

    source.refresh();

    // The list and three calendars each change the cache, and the run's end
    // delivers them as one change.
    QTRY_VERIFY_WITH_TIMEOUT(!source.syncing(), 5000);
    QCOMPARE(changed.size(), 1);
    QTest::qWait(400);
    QCOMPARE(changed.size(), 1);
}

void TestGoogleSource::createdEventShowsWithoutASync()
{
    SyncHarness harness(*m_cache);
    harness.store.secrets.insert(kAccount.id, u"rt"_s);
    harness.apiServer.handler = [](const FakeHttpServer::Request &request) {
        if (request.method == "POST")
            return FakeHttpServer::Response(
                200, R"({"id":"made","status":"confirmed","summary":"Pottery",
                         "start":{"dateTime":"2026-10-09T18:00:00Z"},
                         "end":{"dateTime":"2026-10-09T20:00:00Z"}})");
        return FakeHttpServer::Response(404, "{}");
    };
    GoogleSource source(*m_cache, {kAccount});
    source.setSync(&harness.sync);
    QSignalSpy changed(&source, &CalendarSource::changed);

    EventDraft draft;
    draft.summary = u"Pottery"_s;
    draft.start = QDateTime(QDate(2026, 10, 9), QTime(18, 0), QTimeZone::UTC);
    draft.end = draft.start.addSecs(7200);
    draft.calendarId = source.calendars().first().id;
    QString error = u"unset"_s;
    source.createEvent(draft, [&error](const QString &e) { error = e; });

    QTRY_COMPARE_WITH_TIMEOUT(error, QString(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!changed.isEmpty(), 5000);
    const QList<Event> events =
        source.eventsBetween(draft.start.addDays(-1), draft.end.addDays(1), QTimeZone::UTC);
    QVERIFY(std::any_of(events.cbegin(), events.cend(),
                        [](const Event &e) { return e.summary == u"Pottery"; }));
}

void TestGoogleSource::createInUnknownCalendarFails()
{
    SyncHarness harness(*m_cache);
    GoogleSource source(*m_cache, {kAccount});
    source.setSync(&harness.sync);
    EventDraft draft;
    draft.calendarId = u"google/nobody@example.com/x"_s;
    QString error;
    source.createEvent(draft, [&error](const QString &e) { error = e; });
    QVERIFY(!error.isEmpty());
    QVERIFY(harness.apiServer.requests.isEmpty());
}

void TestGoogleSource::deleteAimsAtTheOccurrenceOrTheSeries()
{
    // An all-day series: its occurrences are addressed by date.
    QVERIFY(m_cache->storeEvents(
        kAccount, u"mine"_s, {parsed(R"({"id":"bins","summary":"Bins","start":{"date":"2026-10-05"},
                    "end":{"date":"2026-10-06"},"recurrence":["RRULE:FREQ=WEEKLY"]})")}));
    SyncHarness harness(*m_cache);
    harness.store.secrets.insert(kAccount.id, u"rt"_s);
    harness.apiServer.handler = [](const FakeHttpServer::Request &) {
        return FakeHttpServer::Response(204, "");
    };
    GoogleSource source(*m_cache, {kAccount});
    source.setSync(&harness.sync);
    const QDateTime from(QDate(2026, 10, 5), QTime(0, 0), QTimeZone::UTC);
    const auto bins = [&] {
        QList<Event> found;
        for (const Event &e : source.eventsBetween(from, from.addDays(14), QTimeZone::UTC))
            if (e.summary == u"Bins")
                found.append(e);
        return found;
    };
    const auto remove = [&](const Event &event, bool wholeSeries) {
        QString error = u"unset"_s;
        source.deleteEvent(event, wholeSeries, [&error](const QString &e) { error = e; });
        [&] { QTRY_COMPARE_WITH_TIMEOUT(error, QString(), 5000); }();
        return QString::fromUtf8(harness.apiServer.requests.last().target);
    };
    QCOMPARE(bins().size(), 2);

    // The second Monday alone, by its all-day instance id.
    QVERIFY(remove(bins().at(1), false).contains("/events/bins_20261012?"));
    QCOMPARE(bins().size(), 1);
    QCOMPARE(bins().first().start.date(), QDate(2026, 10, 5));

    // Then the whole series, by the series' own id.
    QVERIFY(remove(bins().first(), true).contains("/events/bins?"));
    QVERIFY(bins().isEmpty());
}

void TestGoogleSource::answerAimsAtTheTimedOccurrence()
{
    // The daily standup from init, as an invitation.
    QVERIFY(
        m_cache->storeEvents(kAccount, u"mine"_s, {parsed(R"({"id":"standup","summary":"Standup",
                    "start":{"dateTime":"2026-10-05T09:30:00-04:00","timeZone":"America/New_York"},
                    "end":{"dateTime":"2026-10-05T09:45:00-04:00","timeZone":"America/New_York"},
                    "recurrence":["RRULE:FREQ=DAILY;COUNT=5"],
                    "attendees":[{"email":"me@example.com","self":true}]})")}));
    SyncHarness harness(*m_cache);
    harness.store.secrets.insert(kAccount.id, u"rt"_s);
    harness.apiServer.handler = [](const FakeHttpServer::Request &) {
        return FakeHttpServer::Response(200, R"({"id":"standup_20261006T133000Z",
            "recurringEventId":"standup","status":"confirmed","summary":"Standup",
            "originalStartTime":{"dateTime":"2026-10-06T13:30:00Z"},
            "start":{"dateTime":"2026-10-06T13:30:00Z"},"end":{"dateTime":"2026-10-06T13:45:00Z"},
            "attendees":[{"email":"me@example.com","self":true,"responseStatus":"accepted"}]})");
    };
    GoogleSource source(*m_cache, {kAccount});
    source.setSync(&harness.sync);
    const QDateTime from(QDate(2026, 10, 6), QTime(0, 0), kNewYork);
    const QList<Event> tuesday = source.eventsBetween(from, from.addDays(1), kNewYork);
    QCOMPARE(tuesday.size(), 2); // the standup and lunch
    const Event standup = tuesday.at(0).summary == u"Standup" ? tuesday.at(0) : tuesday.at(1);

    QString error = u"unset"_s;
    source.respond(standup, u"accepted"_s, false, [&error](const QString &e) { error = e; });
    QTRY_COMPARE_WITH_TIMEOUT(error, QString(), 5000);

    // Addressed by the occurrence's UTC start, not the series or the local time.
    QVERIFY(harness.apiServer.requests.last().target.contains("/events/standup_20261006T133000Z?"));
    QCOMPARE(harness.apiServer.requests.last().method, QByteArray("PATCH"));
}

void TestGoogleSource::editPatchesOnlyWhatChanged()
{
    QVERIFY(m_cache->storeEvents(kAccount, u"mine"_s, {parsed(R"({"id":"review","summary":"Review",
                    "start":{"dateTime":"2026-10-06T15:00:00Z"},
                    "end":{"dateTime":"2026-10-06T16:00:00Z"},
                    "attendees":[{"email":"me@example.com","self":true,"responseStatus":"accepted"},
                                 {"email":"pat@example.com","responseStatus":"accepted"},
                                 {"email":"sam@example.com","responseStatus":"declined"}]})")}));
    SyncHarness harness(*m_cache);
    harness.store.secrets.insert(kAccount.id, u"rt"_s);
    harness.apiServer.handler = [](const FakeHttpServer::Request &) {
        return FakeHttpServer::Response(200, R"({"id":"review","status":"confirmed"})");
    };
    GoogleSource source(*m_cache, {kAccount});
    source.setSync(&harness.sync);
    const QDateTime from(QDate(2026, 10, 6), QTime(0, 0), QTimeZone::UTC);
    const QList<Event> events = source.eventsBetween(from, from.addDays(1), QTimeZone::UTC);
    const auto review = std::find_if(events.cbegin(), events.cend(),
                                     [](const Event &e) { return e.summary == u"Review"; });
    QVERIFY(review != events.cend());

    EventEdit edit;
    edit.summary = u"Design review"_s;
    edit.start = QDateTime(QDate(2026, 10, 6), QTime(10, 0), kNewYork);
    edit.end = QDateTime(QDate(2026, 10, 6), QTime(11, 30), kNewYork);
    edit.guests = QStringList{u"pat@example.com"_s, u"alex@example.com"_s};
    edit.videoCall = true;
    QString error = u"unset"_s;
    source.updateEvent(*review, edit, EditScope::ThisEvent,
                       [&error](const QString &e) { error = e; });
    QTRY_COMPARE_WITH_TIMEOUT(error, QString(), 5000);

    const FakeHttpServer::Request request = harness.apiServer.requests.last();
    QCOMPARE(request.method, QByteArray("PATCH"));
    // A video call needs Google told that the body may carry one.
    QVERIFY(request.target.contains("conferenceDataVersion=1"));
    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    QCOMPARE(body[u"summary"].toString(), u"Design review"_s);
    QVERIFY(!body.contains(u"location"));
    QCOMPARE(body[u"start"][u"dateTime"].toString(), u"2026-10-06T10:00:00-04:00"_s);
    QCOMPARE(body[u"start"][u"timeZone"].toString(), u"America/New_York"_s);
    QCOMPARE(body[u"end"][u"dateTime"].toString(), u"2026-10-06T11:30:00-04:00"_s);
    QCOMPARE(
        body[u"conferenceData"][u"createRequest"][u"conferenceSolutionKey"][u"type"].toString(),
        u"hangoutsMeet"_s);
    // The user and Pat stay, Pat keeping the answer; Sam goes; Alex is new.
    const QJsonArray guests = body[u"attendees"].toArray();
    QCOMPARE(guests.size(), 3);
    QVERIFY(guests.at(0)[u"self"].toBool());
    QCOMPARE(guests.at(1)[u"email"].toString(), u"pat@example.com"_s);
    QCOMPARE(guests.at(1)[u"responseStatus"].toString(), u"accepted"_s);
    QCOMPARE(guests.at(2)[u"email"].toString(), u"alex@example.com"_s);
}

void TestGoogleSource::editingTheSeriesKeepsItsFirstDay()
{
    SyncHarness harness(*m_cache);
    harness.store.secrets.insert(kAccount.id, u"rt"_s);
    harness.apiServer.handler = [](const FakeHttpServer::Request &) {
        return FakeHttpServer::Response(200, R"({"id":"standup","status":"confirmed"})");
    };
    GoogleSource source(*m_cache, {kAccount});
    source.setSync(&harness.sync);
    const QDateTime from(QDate(2026, 10, 7), QTime(0, 0), kNewYork);
    const QList<Event> wednesday = source.eventsBetween(from, from.addDays(1), kNewYork);
    const auto standup = std::find_if(wednesday.cbegin(), wednesday.cend(),
                                      [](const Event &e) { return e.summary == u"Standup"; });
    QVERIFY(standup != wednesday.cend());

    // Wednesday's standup moved to 10:15, for the whole series, which began on Monday.
    EventEdit edit;
    edit.start = QDateTime(QDate(2026, 10, 7), QTime(10, 15), kNewYork);
    edit.end = QDateTime(QDate(2026, 10, 7), QTime(10, 30), kNewYork);
    QString error = u"unset"_s;
    source.updateEvent(*standup, edit, EditScope::AllEvents,
                       [&error](const QString &e) { error = e; });
    QTRY_COMPARE_WITH_TIMEOUT(error, QString(), 5000);

    QVERIFY(harness.apiServer.requests.last().target.contains("/events/standup?"));
    const QJsonObject body =
        QJsonDocument::fromJson(harness.apiServer.requests.last().body).object();
    QCOMPARE(body[u"start"][u"dateTime"].toString(), u"2026-10-05T10:15:00-04:00"_s);
    QCOMPARE(body[u"end"][u"dateTime"].toString(), u"2026-10-05T10:30:00-04:00"_s);
}

void TestGoogleSource::editingOnlyTheEndKeepsTheSeriesClock()
{
    SyncHarness harness(*m_cache);
    harness.store.secrets.insert(kAccount.id, u"rt"_s);
    harness.apiServer.handler = [](const FakeHttpServer::Request &) {
        return FakeHttpServer::Response(200, R"({"id":"standup","status":"confirmed"})");
    };
    GoogleSource source(*m_cache, {kAccount});
    source.setSync(&harness.sync);
    // Read in UTC, as a viewer far from New York would.
    const QDateTime from(QDate(2026, 10, 7), QTime(0, 0), QTimeZone::UTC);
    const QList<Event> events = source.eventsBetween(from, from.addDays(1), QTimeZone::UTC);
    const auto standup = std::find_if(events.cbegin(), events.cend(),
                                      [](const Event &e) { return e.summary == u"Standup"; });
    QVERIFY(standup != events.cend());

    EventEdit edit;
    edit.end = standup->end.addSecs(15 * 60);
    QString error = u"unset"_s;
    source.updateEvent(*standup, edit, EditScope::AllEvents,
                       [&error](const QString &e) { error = e; });
    QTRY_COMPARE_WITH_TIMEOUT(error, QString(), 5000);
    const QJsonObject body =
        QJsonDocument::fromJson(harness.apiServer.requests.last().body).object();
    // Still Monday 9:30 in New York, now half an hour long.
    QCOMPARE(body[u"start"][u"dateTime"].toString(), u"2026-10-05T09:30:00-04:00"_s);
    QCOMPARE(body[u"start"][u"timeZone"].toString(), u"America/New_York"_s);
    QCOMPARE(body[u"end"][u"dateTime"].toString(), u"2026-10-05T10:00:00-04:00"_s);
}

void TestGoogleSource::allDayClearsTheTime()
{
    SyncHarness harness(*m_cache);
    harness.store.secrets.insert(kAccount.id, u"rt"_s);
    harness.apiServer.handler = [](const FakeHttpServer::Request &) {
        return FakeHttpServer::Response(200, R"({"id":"lunch","status":"confirmed"})");
    };
    GoogleSource source(*m_cache, {kAccount});
    source.setSync(&harness.sync);
    const QDateTime from(QDate(2026, 10, 6), QTime(0, 0), kNewYork);
    const QList<Event> events = source.eventsBetween(from, from.addDays(1), kNewYork);
    const auto lunch = std::find_if(events.cbegin(), events.cend(),
                                    [](const Event &e) { return e.summary == u"Lunch"; });
    QVERIFY(lunch != events.cend());

    EventEdit edit;
    edit.allDay = true;
    QString error = u"unset"_s;
    source.updateEvent(*lunch, edit, EditScope::ThisEvent,
                       [&error](const QString &e) { error = e; });
    QTRY_COMPARE_WITH_TIMEOUT(error, QString(), 5000);
    const QJsonObject body =
        QJsonDocument::fromJson(harness.apiServer.requests.last().body).object();
    // Google merges what it is sent, so the time is cleared outright.
    QCOMPARE(body[u"start"][u"date"].toString(), u"2026-10-06"_s);
    QVERIFY(body[u"start"].toObject().contains(u"dateTime"));
    QVERIFY(body[u"start"][u"dateTime"].isNull());
    QCOMPARE(body[u"end"][u"date"].toString(), u"2026-10-07"_s);
}

QList<FakeHttpServer::Request>
TestGoogleSource::splitStandup(decltype(FakeHttpServer::handler) handler, QString &error)
{
    SyncHarness harness(*m_cache);
    harness.store.secrets.insert(kAccount.id, u"rt"_s);
    harness.apiServer.handler = std::move(handler);
    GoogleSource source(*m_cache, {kAccount});
    source.setSync(&harness.sync);
    const QDateTime from(QDate(2026, 10, 7), QTime(0, 0), kNewYork);
    const QList<Event> events = source.eventsBetween(from, from.addDays(1), kNewYork);
    const auto standup = std::find_if(events.cbegin(), events.cend(),
                                      [](const Event &e) { return e.summary == u"Standup"; });
    if (standup == events.cend())
        return {};
    EventEdit edit;
    edit.summary = u"Sync"_s;
    bool done = false;
    source.updateEvent(*standup, edit, EditScope::ThisAndFollowing,
                       [&error, &done](const QString &e) {
                           error = e;
                           done = true;
                       });
    if (!QTest::qWaitFor([&done] { return done; }, 5000))
        error = u"no answer"_s;
    return harness.apiServer.requests;
}

void TestGoogleSource::followingEditsSplitTheSeries()
{
    // Occurrences moved on their own, one before the split and one after.
    QVERIFY(m_cache->storeEvents(
        kAccount, u"mine"_s,
        {parsed(R"({"id":"standup_20261006T133000Z","recurringEventId":"standup",
            "status":"confirmed","summary":"Standup",
            "originalStartTime":{"dateTime":"2026-10-06T13:30:00Z"},
            "start":{"dateTime":"2026-10-06T14:30:00Z"},"end":{"dateTime":"2026-10-06T14:45:00Z"}})"),
         parsed(R"({"id":"standup_20261008T133000Z","recurringEventId":"standup",
            "status":"confirmed","summary":"Standup",
            "originalStartTime":{"dateTime":"2026-10-08T13:30:00Z"},
            "start":{"dateTime":"2026-10-08T14:30:00Z"},"end":{"dateTime":"2026-10-08T14:45:00Z"}})")}));
    QString error;
    const QList<FakeHttpServer::Request> requests = splitStandup(
        [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response(request.method == "DELETE" ? 204 : 200,
                                            request.method == "POST"
                                                ? R"({"id":"split","status":"confirmed"})"
                                                : R"({"id":"standup","status":"confirmed"})");
        },
        error);
    QCOMPARE(error, QString());
    QCOMPARE(requests.size(), 3);

    // A new series from Wednesday with the three standups left, under the new name...
    QCOMPARE(requests[0].method, QByteArray("POST"));
    const QJsonObject created = QJsonDocument::fromJson(requests[0].body).object();
    QCOMPARE(created[u"summary"].toString(), u"Sync"_s);
    QCOMPARE(created[u"start"][u"dateTime"].toString(), u"2026-10-07T09:30:00-04:00"_s);
    QCOMPARE(created[u"start"][u"timeZone"].toString(), u"America/New_York"_s);
    QVERIFY(!created[u"start"].toObject().contains(u"date"));
    QCOMPARE(created[u"recurrence"].toArray(), QJsonArray{u"RRULE:FREQ=DAILY;COUNT=3"_s});
    QCOMPARE(created[u"id"].toString().size(), 32);

    // ...then the old one ends on Tuesday...
    QCOMPARE(requests[1].method, QByteArray("PATCH"));
    QVERIFY(requests[1].target.contains("/events/standup?"));
    QCOMPARE(
        QJsonDocument::fromJson(requests[1].body).object(),
        (QJsonObject{{u"recurrence"_s, QJsonArray{u"RRULE:FREQ=DAILY;UNTIL=20261007T132959Z"_s}}}));

    // ...and Thursday's moved standup, which it no longer has, goes.
    QCOMPARE(requests[2].method, QByteArray("DELETE"));
    QVERIFY(requests[2].target.contains("/events/standup_20261008T133000Z?"));
}

void TestGoogleSource::splitStopsWhenTheNewSeriesFails()
{
    QString error;
    const QList<FakeHttpServer::Request> requests = splitStandup(
        [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response(403, R"({"error":{"message":"Forbidden"}})");
        },
        error);
    QVERIFY(error.contains(u"Forbidden"_s));
    // The old series is left whole.
    QCOMPARE(requests.size(), 1);
    QCOMPARE(requests[0].method, QByteArray("POST"));
}

void TestGoogleSource::splitGoesOnWhenTheNewSeriesExists()
{
    // An earlier try created it, though its answer was lost.
    QString error;
    const QList<FakeHttpServer::Request> requests = splitStandup(
        [](const FakeHttpServer::Request &request) {
            return request.method == "POST"
                       ? FakeHttpServer::Response(409, R"({"error":{"message":"duplicate"}})")
                       : FakeHttpServer::Response(200, R"({"id":"standup","status":"confirmed"})");
        },
        error);
    QCOMPARE(error, QString());
    QCOMPARE(requests.size(), 2);
    QCOMPARE(requests[1].method, QByteArray("PATCH"));
}

void TestGoogleSource::splitKeepsWhatTheOccurrenceHad()
{
    // Wednesday's standup was moved to another room on its own.
    QVERIFY(m_cache->storeEvents(
        kAccount, u"mine"_s,
        {parsed(R"({"id":"standup_20261007T133000Z","recurringEventId":"standup",
            "status":"confirmed","summary":"Standup","location":"Room 2",
            "originalStartTime":{"dateTime":"2026-10-07T13:30:00Z"},
            "start":{"dateTime":"2026-10-07T09:30:00-04:00","timeZone":"America/New_York"},
            "end":{"dateTime":"2026-10-07T09:45:00-04:00","timeZone":"America/New_York"}})")}));
    QString error;
    const QList<FakeHttpServer::Request> requests = splitStandup(
        [](const FakeHttpServer::Request &request) {
            return FakeHttpServer::Response(request.method == "DELETE" ? 204 : 200,
                                            R"({"id":"standup","status":"confirmed"})");
        },
        error);
    QCOMPARE(error, QString());
    const QJsonObject created = QJsonDocument::fromJson(requests.first().body).object();
    QCOMPARE(created[u"summary"].toString(), u"Sync"_s);
    QCOMPARE(created[u"location"].toString(), u"Room 2"_s);
    // The changed occurrence was the old series', so it goes.
    QVERIFY(requests.last().target.contains("/events/standup_20261007T133000Z?"));
}

void TestGoogleSource::splitAllDaySeriesByDates()
{
    QVERIFY(m_cache->storeEvents(
        kAccount, u"mine"_s, {parsed(R"({"id":"bins","summary":"Bins","start":{"date":"2026-10-05"},
                    "end":{"date":"2026-10-06"},"recurrence":["RRULE:FREQ=WEEKLY"]})")}));
    SyncHarness harness(*m_cache);
    harness.store.secrets.insert(kAccount.id, u"rt"_s);
    harness.apiServer.handler = [](const FakeHttpServer::Request &) {
        return FakeHttpServer::Response(200, R"({"id":"bins","status":"confirmed"})");
    };
    GoogleSource source(*m_cache, {kAccount});
    source.setSync(&harness.sync);
    const QDateTime from(QDate(2026, 10, 19), QTime(0, 0), kNewYork);
    const QList<Event> events = source.eventsBetween(from, from.addDays(1), kNewYork);
    const auto bins = std::find_if(events.cbegin(), events.cend(),
                                   [](const Event &e) { return e.summary == u"Bins"; });
    QVERIFY(bins != events.cend());

    EventEdit edit;
    edit.summary = u"Recycling"_s;
    QString error = u"unset"_s;
    source.updateEvent(*bins, edit, EditScope::ThisAndFollowing,
                       [&error](const QString &e) { error = e; });
    QTRY_COMPARE_WITH_TIMEOUT(error, QString(), 5000);
    const QList<FakeHttpServer::Request> &requests = harness.apiServer.requests;
    QCOMPARE(requests.size(), 2);
    const QJsonObject created = QJsonDocument::fromJson(requests[0].body).object();
    QCOMPARE(created[u"start"][u"date"].toString(), u"2026-10-19"_s);
    QCOMPARE(created[u"end"][u"date"].toString(), u"2026-10-20"_s);
    QVERIFY(!created[u"start"].toObject().contains(u"dateTime"));
    QCOMPARE(created[u"recurrence"].toArray(), QJsonArray{u"RRULE:FREQ=WEEKLY"_s});
    QCOMPARE(QJsonDocument::fromJson(requests[1].body).object(),
             (QJsonObject{{u"recurrence"_s, QJsonArray{u"RRULE:FREQ=WEEKLY;UNTIL=20261018"_s}}}));
}

void TestGoogleSource::moveShiftsTheOccurrenceOrTheSeries()
{
    SyncHarness harness(*m_cache);
    harness.store.secrets.insert(kAccount.id, u"rt"_s);
    harness.apiServer.handler = [](const FakeHttpServer::Request &) {
        return FakeHttpServer::Response(200, R"({"id":"x","status":"confirmed",
            "start":{"dateTime":"2026-10-06T14:30:00Z"},"end":{"dateTime":"2026-10-06T14:45:00Z"}})");
    };
    GoogleSource source(*m_cache, {kAccount});
    source.setSync(&harness.sync);
    const QDateTime from(QDate(2026, 10, 6), QTime(0, 0), kNewYork);
    const QList<Event> tuesday = source.eventsBetween(from, from.addDays(1), kNewYork);
    const auto standup = std::find_if(tuesday.cbegin(), tuesday.cend(),
                                      [](const Event &e) { return e.summary == u"Standup"; });
    QVERIFY(standup != tuesday.cend());
    // An hour later, in Berlin terms, to show the series keeps its own zone.
    const QTimeZone berlin("Europe/Berlin");
    const QDateTime start = standup->start.addSecs(3600).toTimeZone(berlin);
    const QDateTime end = standup->end.addSecs(3600).toTimeZone(berlin);

    QString error;
    const auto move = [&](bool wholeSeries) {
        error = u"unset"_s;
        source.moveEvent(*standup, start, end, wholeSeries,
                         [&error](const QString &e) { error = e; });
    };
    const auto body = [&harness] {
        return QJsonDocument::fromJson(harness.apiServer.requests.last().body).object();
    };

    move(false);
    QTRY_COMPARE_WITH_TIMEOUT(error, QString(), 5000);
    QCOMPARE(harness.apiServer.requests.last().method, QByteArray("PATCH"));
    QVERIFY(harness.apiServer.requests.last().target.contains("/events/standup_20261006T133000Z?"));
    QCOMPARE(body()[u"start"][u"dateTime"].toString(), u"2026-10-06T10:30:00-04:00"_s);
    QCOMPARE(body()[u"start"][u"timeZone"].toString(), u"America/New_York"_s);
    QCOMPARE(body()[u"end"][u"dateTime"].toString(), u"2026-10-06T10:45:00-04:00"_s);

    // The whole series moves by the same hour from its first occurrence.
    move(true);
    QTRY_COMPARE_WITH_TIMEOUT(error, QString(), 5000);
    QVERIFY(harness.apiServer.requests.last().target.contains("/events/standup?"));
    QCOMPARE(body()[u"start"][u"dateTime"].toString(), u"2026-10-05T10:30:00-04:00"_s);
    QCOMPARE(body()[u"end"][u"dateTime"].toString(), u"2026-10-05T10:45:00-04:00"_s);
}

void TestGoogleSource::seriesMoveKeepsTheWallClockAcrossDst()
{
    QVERIFY(m_cache->storeEvents(kAccount, u"mine"_s, {parsed(R"({"id":"swim","summary":"Swim",
                    "start":{"dateTime":"2026-03-27T10:00:00+01:00","timeZone":"Europe/Berlin"},
                    "end":{"dateTime":"2026-03-27T11:00:00+01:00","timeZone":"Europe/Berlin"},
                    "recurrence":["RRULE:FREQ=DAILY;COUNT=5"]})")}));
    SyncHarness harness(*m_cache);
    harness.store.secrets.insert(kAccount.id, u"rt"_s);
    harness.apiServer.handler = [](const FakeHttpServer::Request &) {
        return FakeHttpServer::Response(200, R"({"id":"swim","status":"confirmed"})");
    };
    GoogleSource source(*m_cache, {kAccount});
    source.setSync(&harness.sync);
    const QTimeZone berlin("Europe/Berlin");
    const QDateTime saturday(QDate(2026, 3, 28), QTime(0, 0), berlin);
    const QList<Event> events = source.eventsBetween(saturday, saturday.addDays(1), berlin);
    const auto swim = std::find_if(events.cbegin(), events.cend(),
                                   [](const Event &e) { return e.summary == u"Swim"; });
    QVERIFY(swim != events.cend());

    // Saturday's swim dragged to Sunday, the first day of summer time.
    QString error = u"unset"_s;
    source.moveEvent(*swim, swim->start.addDays(1), swim->end.addDays(1), true,
                     [&error](const QString &e) { error = e; });
    QTRY_COMPARE_WITH_TIMEOUT(error, QString(), 5000);

    // The series still starts at 10:00, a day later, not at 09:00.
    const QJsonObject body =
        QJsonDocument::fromJson(harness.apiServer.requests.last().body).object();
    QCOMPARE(body[u"start"][u"dateTime"].toString(), u"2026-03-28T10:00:00+01:00"_s);
    QCOMPARE(body[u"end"][u"dateTime"].toString(), u"2026-03-28T11:00:00+01:00"_s);
}

void TestGoogleSource::actionsNeedAKnownCalendar()
{
    SyncHarness harness(*m_cache);
    GoogleSource source(*m_cache, {kAccount});
    source.setSync(&harness.sync);
    Event stranger;
    stranger.calendarId = u"google/nobody@example.com/x"_s;
    stranger.eventId = u"e"_s;
    Outcome answered;
    Outcome deleted;
    source.respond(stranger, u"accepted"_s, false, [&answered](const Outcome &o) { answered = o; });
    source.deleteEvent(stranger, false, [&deleted](const Outcome &o) { deleted = o; });
    QVERIFY(!answered.isEmpty());
    QVERIFY(!deleted.isEmpty());
    // The account is gone and will not come back, so nothing waits for it.
    QVERIFY(!answered.retry);
    QVERIFY(!deleted.retry);
    QVERIFY(harness.apiServer.requests.isEmpty());
}

void TestGoogleSource::reportDescribesEachAccount()
{
    QVERIFY(m_cache->recordAccountSync(kAccount, {}));
    QVERIFY(m_cache->recordCalendarError(kAccount, u"mine"_s, u"Rate limit"_s));
    const Account other{u"google"_s, u"other@example.com"_s};
    QVERIFY(m_cache->recordAccountSync(other, u"keyring is locked"_s));
    const GoogleSource source(*m_cache, {kAccount, other});

    const QVariantList report = source.syncReport();
    QCOMPARE(report.size(), 2);
    const QVariantMap mine = report.at(0).toMap();
    QCOMPARE(mine.value(u"account"_s).toString(), kAccount.id);
    QVERIFY(mine.value(u"lastSynced"_s).toDateTime().isValid());
    QCOMPARE(mine.value(u"problems"_s).toStringList(), QStringList{u"MINE: Rate limit"_s});
    QCOMPARE(report.at(1).toMap().value(u"error"_s).toString(), u"keyring is locked"_s);
}

void TestGoogleSource::accountsCanChangeWhileRunning()
{
    GoogleSource source(*m_cache, {});
    QVERIFY(source.calendars().isEmpty());
    QSignalSpy changed(&source, &CalendarSource::changed);

    QVERIFY(m_cache->recordAccountSync(kAccount, u"keyring is locked"_s));
    source.setAccounts({kAccount});

    QCOMPARE(source.calendars().size(), 2);
    QVERIFY(!changed.isEmpty());
    QVERIFY(source.lastError().contains(u"keyring is locked"_s));

    // A removed account takes its events and its error with it.
    changed.clear();
    source.setAccounts({});
    QVERIFY(source.calendars().isEmpty());
    QVERIFY(source.lastError().isEmpty());
    QVERIFY(!source.lastSynced().isValid());
    QCOMPARE(changed.size(), 1);
}

void TestGoogleSource::statusStartsFromTheCache()
{
    QVERIFY(m_cache->recordAccountSync(kAccount, {}));
    QVERIFY(m_cache->recordAccountSync(kAccount, u"keyring is locked"_s));

    const GoogleSource source(*m_cache, {kAccount});

    QVERIFY(source.lastSynced().isValid());
    QCOMPARE(source.lastError(), u"me@example.com: keyring is locked"_s);
    QVERIFY(!source.syncing());
}

void TestGoogleSource::statusFollowsARefresh()
{
    const Account other{u"google"_s, u"other@example.com"_s};
    SyncHarness harness(*m_cache);
    harness.store.secrets.insert(kAccount.id, u"rt"_s);
    GoogleSource source(*m_cache, {kAccount, other});
    source.setSync(&harness.sync);
    QSignalSpy status(&source, &CalendarSource::statusChanged);

    source.refresh();
    QVERIFY(source.syncing());
    QTRY_VERIFY_WITH_TIMEOUT(!source.syncing(), 5000);

    // The other account has no token, so the run failed and keeps no new time.
    QVERIFY(source.lastError().startsWith(u"other@example.com: "_s));
    QVERIFY(!source.lastSynced().isValid());

    harness.store.secrets.insert(other.id, u"rt"_s);
    source.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!source.syncing(), 5000);
    QCOMPARE(source.lastError(), QString());
    QVERIFY(source.lastSynced().isValid());
    QCOMPARE(status.size(), 4);
}

void TestGoogleSource::calendarErrorShowsAfterRestart()
{
    QVERIFY(m_cache->recordAccountSync(kAccount, {}));
    QVERIFY(m_cache->recordCalendarError(kAccount, u"mine"_s, u"Backend Error"_s));

    const GoogleSource source(*m_cache, {kAccount});

    QCOMPARE(source.lastError(), u"me@example.com: MINE: Backend Error"_s);
}

void TestGoogleSource::overlappingRefreshesReportEachErrorOnce()
{
    SyncHarness harness(*m_cache);
    GoogleSource source(*m_cache, {kAccount});
    source.setSync(&harness.sync);
    QSignalSpy errors(&source, &CalendarSource::errorOccurred);

    // No stored token, so the shared run fails once for both refreshes.
    source.refresh();
    source.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!source.syncing(), 5000);

    QCOMPARE(errors.size(), 1);
    QCOMPARE(source.lastError().count(u'\n'), 0);
}

QTEST_GUILESS_MAIN(TestGoogleSource)
#include "tst_googlesource.moc"
