#include "FakeHttpServer.h"
#include "FakeTokenStore.h"

#include "callie/GoogleCache.h"
#include "callie/GoogleCalendarApi.h"
#include "callie/GoogleSource.h"
#include "callie/GoogleSync.h"
#include "callie/GoogleTokenProvider.h"

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
    void syncReportsOneChangePerBurst();
    void createdEventShowsWithoutASync();
    void createInUnknownCalendarFails();
    void deleteAimsAtTheOccurrenceOrTheSeries();
    void answerAimsAtTheTimedOccurrence();
    void actionsNeedAKnownCalendar();
    void statusStartsFromTheCache();
    void reportDescribesEachAccount();
    void accountsCanChangeWhileRunning();
    void statusFollowsARefresh();
    void calendarErrorShowsAfterRestart();
    void overlappingRefreshesReportEachErrorOnce();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<GoogleCache> m_cache;
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
    QCOMPARE(harness.store.reads, 1);
    QCOMPARE(harness.apiServer.requests.size(), 2);
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

void TestGoogleSource::actionsNeedAKnownCalendar()
{
    SyncHarness harness(*m_cache);
    GoogleSource source(*m_cache, {kAccount});
    source.setSync(&harness.sync);
    Event stranger;
    stranger.calendarId = u"google/nobody@example.com/x"_s;
    stranger.eventId = u"e"_s;
    QString answered;
    QString deleted;
    source.respond(stranger, u"accepted"_s, false, [&answered](const QString &e) { answered = e; });
    source.deleteEvent(stranger, false, [&deleted](const QString &e) { deleted = e; });
    QVERIFY(!answered.isEmpty());
    QVERIFY(!deleted.isEmpty());
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

    source.setAccounts({kAccount});

    QCOMPARE(source.calendars().size(), 2);
    QVERIFY(!changed.isEmpty());
    source.setAccounts({});
    QVERIFY(source.calendars().isEmpty());
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
