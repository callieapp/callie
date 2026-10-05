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
    void refreshWithoutSyncRereadsCache();
    void refreshSyncsAndReportsErrorsPerAccount();
    void refreshWhileSyncingStartsNothingNew();
    void statusStartsFromTheCache();
    void statusFollowsARefresh();
    void calendarErrorShowsAfterRestart();

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

QTEST_GUILESS_MAIN(TestGoogleSource)
#include "tst_googlesource.moc"
