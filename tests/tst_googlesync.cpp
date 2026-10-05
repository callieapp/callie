#include "FakeHttpServer.h"
#include "FakeTokenStore.h"

#include "callie/GoogleCache.h"
#include "callie/GoogleCalendarApi.h"
#include "callie/GoogleSync.h"
#include "callie/GoogleTokenProvider.h"

#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

const GoogleClientConfig kClient{u"test-id"_s, u"test-secret"_s};
const Account kAccount{u"google"_s, u"me@example.com"_s};
const QByteArray kCalendarList =
    R"({"items":[{"id":"me@example.com","summary":"Me"},{"id":"team","summary":"Team"}]})";

/// Answers each API path from its own queue, falling back to the last
/// response for that path, since calendars sync in parallel.
class FakeGoogle
{
public:
    explicit FakeGoogle(FakeHttpServer &server)
    {
        server.handler = [this](const FakeHttpServer::Request &request) {
            const QUrl url(QString::fromUtf8(request.target));
            const QString path = url.path(QUrl::FullyEncoded);
            requests.append({path, QUrlQuery(url)});
            QList<FakeHttpServer::Response> &queue = responses[path];
            if (queue.isEmpty())
                return FakeHttpServer::Response(404, R"({"error":{"message":"no route"}})");
            return queue.size() > 1 ? queue.takeFirst() : queue.first();
        };
    }

    void on(const QString &path, int status, const QByteArray &body)
    {
        responses[u"/v3/"_s + path].append({status, body});
    }

    [[nodiscard]] QStringList syncTokensSent(const QString &path) const
    {
        QStringList tokens;
        for (const auto &[requestPath, query] : requests) {
            if (requestPath == u"/v3/"_s + path)
                tokens.append(query.queryItemValue(u"syncToken"_s));
        }
        return tokens;
    }

    [[nodiscard]] int count(const QString &path) const
    {
        return int(std::count_if(requests.cbegin(), requests.cend(), [&](const auto &request) {
            return request.first == u"/v3/"_s + path;
        }));
    }

    QHash<QString, QList<FakeHttpServer::Response>> responses;
    QList<std::pair<QString, QUrlQuery>> requests;
};

QByteArray events(const char *items, const char *syncToken)
{
    return QByteArray(R"({"items":[)") + items + R"(],"nextSyncToken":")" + syncToken + R"("})";
}

} // namespace

class TestGoogleSync : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void firstSyncIsFullThenIncremental();
    void expiredSyncTokenFallsBackToFullSync();
    void fullSyncKeepsCancelledOccurrences();
    void failedCalendarDoesNotStopOthers();
    void rejectedAccessTokenIsRefreshedOnce();
    void persistentRejectionIsReported();
    void missingRefreshTokenIsReported();
    void concurrentSyncsShareOneRun();
    void removedCalendarIsDropped();
    void calendarListChangeIsSignalled();
    void outcomesAreRecorded();

private:
    QStringList runSync();

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<FakeHttpServer> m_tokenServer;
    std::unique_ptr<FakeHttpServer> m_apiServer;
    std::unique_ptr<FakeGoogle> m_google;
    std::unique_ptr<FakeTokenStore> m_store;
    std::unique_ptr<QNetworkAccessManager> m_network;
    std::unique_ptr<GoogleTokenProvider> m_tokens;
    std::unique_ptr<GoogleCalendarApi> m_api;
    std::unique_ptr<GoogleCache> m_cache;
    std::unique_ptr<GoogleSync> m_sync;
};

void TestGoogleSync::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_tokenServer = std::make_unique<FakeHttpServer>();
    m_tokenServer->respond(200,
                           R"({"access_token":"at-1","expires_in":3600,"token_type":"Bearer"})");
    m_apiServer = std::make_unique<FakeHttpServer>();
    m_google = std::make_unique<FakeGoogle>(*m_apiServer);
    m_store = std::make_unique<FakeTokenStore>();
    m_store->secrets.insert(kAccount.id, u"rt-1"_s);
    m_network = std::make_unique<QNetworkAccessManager>();
    m_tokens = std::make_unique<GoogleTokenProvider>(kClient, *m_store);
    m_tokens->setTokenUrl(m_tokenServer->url(u"/token"_s));
    m_api = std::make_unique<GoogleCalendarApi>(m_network.get());
    m_api->setBaseUrl(m_apiServer->url(u"/v3/"_s));
    m_cache = std::make_unique<GoogleCache>(m_dir->filePath(u"google.sqlite"_s));
    QVERIFY(m_cache->open());
    m_sync = std::make_unique<GoogleSync>(*m_tokens, *m_api, *m_cache);

    m_google->on(u"users/me/calendarList"_s, 200, kCalendarList);
}

void TestGoogleSync::cleanup()
{
    m_sync.reset();
    m_cache.reset();
}

QStringList TestGoogleSync::runSync()
{
    QStringList errors;
    bool done = false;
    m_sync->sync(kAccount, [&](const QStringList &e) {
        errors = e;
        done = true;
    });
    [&] { QTRY_VERIFY_WITH_TIMEOUT(done, 5000); }();
    return errors;
}

void TestGoogleSync::firstSyncIsFullThenIncremental()
{
    m_google->on(u"calendars/me%40example.com/events"_s, 200, events(R"({"id":"a"})", "me-1"));
    m_google->on(u"calendars/me%40example.com/events"_s, 200,
                 events(R"({"id":"a","status":"cancelled"},{"id":"b"})", "me-2"));
    m_google->on(u"calendars/team/events"_s, 200, events(R"({"id":"t"})", "team-1"));
    QSignalSpy changed(m_sync.get(), &GoogleSync::changed);

    QCOMPARE(runSync(), QStringList());
    QCOMPARE(m_cache->calendars(kAccount).size(), 2);
    QCOMPARE(m_cache->events(kAccount, kAccount.id).size(), 1);
    QCOMPARE(m_cache->events(kAccount, u"team"_s).size(), 1);
    // Once for the calendar list, then once per calendar.
    QCOMPARE(changed.size(), 3);

    QCOMPARE(runSync(), QStringList());
    QCOMPARE(m_google->syncTokensSent(u"calendars/me%40example.com/events"_s),
             (QStringList{QString(), u"me-1"_s}));
    QCOMPARE(m_google->syncTokensSent(u"calendars/team/events"_s),
             (QStringList{QString(), u"team-1"_s}));
    const QList<GoogleEvent> mine = m_cache->events(kAccount, kAccount.id);
    QCOMPARE(mine.size(), 1);
    QCOMPARE(mine.first().id, u"b"_s);
    QCOMPARE(m_cache->syncToken(kAccount, kAccount.id), u"me-2"_s);
}

void TestGoogleSync::expiredSyncTokenFallsBackToFullSync()
{
    m_google->on(u"calendars/me%40example.com/events"_s, 200,
                 events(R"({"id":"a"},{"id":"b"})", "me-1"));
    m_google->on(u"calendars/me%40example.com/events"_s, 410,
                 R"({"error":{"code":410,"message":"Sync token is no longer valid"}})");
    m_google->on(u"calendars/me%40example.com/events"_s, 200, events(R"({"id":"c"})", "me-2"));
    m_google->on(u"calendars/team/events"_s, 200, events("", "team-1"));
    QCOMPARE(runSync(), QStringList());

    QCOMPARE(runSync(), QStringList());

    QCOMPARE(m_google->syncTokensSent(u"calendars/me%40example.com/events"_s),
             (QStringList{QString(), u"me-1"_s, QString()}));
    const QList<GoogleEvent> mine = m_cache->events(kAccount, kAccount.id);
    QCOMPARE(mine.size(), 1);
    QCOMPARE(mine.first().id, u"c"_s);
    QCOMPARE(m_cache->syncToken(kAccount, kAccount.id), u"me-2"_s);
}

void TestGoogleSync::fullSyncKeepsCancelledOccurrences()
{
    // Google includes cancelled occurrences of a series in a full sync, since
    // showDeleted and singleEvents are both left false.
    m_google->on(u"calendars/me%40example.com/events"_s, 200,
                 events(R"({"id":"s","recurrence":["RRULE:FREQ=DAILY"]},
                           {"id":"s_1","status":"cancelled","recurringEventId":"s",
                            "originalStartTime":{"dateTime":"2026-10-06T10:00:00Z"}})",
                        "me-1"));
    m_google->on(u"calendars/team/events"_s, 200, events("", "team-1"));

    QCOMPARE(runSync(), QStringList());

    const QList<GoogleEvent> mine = m_cache->events(kAccount, kAccount.id);
    QCOMPARE(mine.size(), 2);
    QVERIFY(mine.at(1).isCancelled());
    QCOMPARE(mine.at(1).recurringEventId, u"s"_s);
}

void TestGoogleSync::failedCalendarDoesNotStopOthers()
{
    m_google->on(u"calendars/me%40example.com/events"_s, 500,
                 R"({"error":{"code":500,"message":"Backend Error"}})");
    m_google->on(u"calendars/team/events"_s, 200, events(R"({"id":"t"})", "team-1"));

    const QStringList errors = runSync();

    QCOMPARE(errors, QStringList{u"Me: Backend Error"_s});
    QCOMPARE(m_cache->events(kAccount, u"team"_s).size(), 1);
    QCOMPARE(m_cache->syncToken(kAccount, kAccount.id), QString());
}

void TestGoogleSync::rejectedAccessTokenIsRefreshedOnce()
{
    m_google->responses.clear();
    m_google->on(u"users/me/calendarList"_s, 401, R"({"error":{"message":"Invalid Credentials"}})");
    m_google->on(u"users/me/calendarList"_s, 200, kCalendarList);
    m_google->on(u"calendars/me%40example.com/events"_s, 200, events("", "me-1"));
    m_google->on(u"calendars/team/events"_s, 200, events("", "team-1"));

    QCOMPARE(runSync(), QStringList());
    QCOMPARE(m_tokenServer->requests.size(), 2);
    QCOMPARE(m_google->count(u"users/me/calendarList"_s), 2);
}

void TestGoogleSync::persistentRejectionIsReported()
{
    m_google->responses.clear();
    m_google->on(u"users/me/calendarList"_s, 401, R"({"error":{"message":"Invalid Credentials"}})");

    const QStringList errors = runSync();

    QCOMPARE(errors.size(), 1);
    QVERIFY(errors.first().contains(u"Invalid Credentials"_s));
    QCOMPARE(m_google->count(u"users/me/calendarList"_s), 2);
}

void TestGoogleSync::missingRefreshTokenIsReported()
{
    m_store->secrets.clear();

    const QStringList errors = runSync();

    QCOMPARE(errors.size(), 1);
    QVERIFY(errors.first().contains(kAccount.id));
    QCOMPARE(m_google->requests.size(), 0);
}

void TestGoogleSync::concurrentSyncsShareOneRun()
{
    m_google->on(u"calendars/me%40example.com/events"_s, 200, events("", "me-1"));
    m_google->on(u"calendars/team/events"_s, 200, events("", "team-1"));

    int done = 0;
    m_sync->sync(kAccount, [&](const QStringList &) { ++done; });
    m_sync->sync(kAccount, [&](const QStringList &) { ++done; });

    QTRY_COMPARE_WITH_TIMEOUT(done, 2, 5000);
    QCOMPARE(m_google->count(u"users/me/calendarList"_s), 1);
}

void TestGoogleSync::removedCalendarIsDropped()
{
    m_google->on(u"calendars/me%40example.com/events"_s, 200, events("", "me-1"));
    m_google->on(u"calendars/team/events"_s, 200, events(R"({"id":"t"})", "team-1"));
    QCOMPARE(runSync(), QStringList());

    m_google->responses[u"/v3/users/me/calendarList"_s] = {
        {200, R"({"items":[{"id":"me@example.com","summary":"Me"}]})"}};
    QCOMPARE(runSync(), QStringList());

    QCOMPARE(m_cache->calendars(kAccount).size(), 1);
    QVERIFY(m_cache->events(kAccount, u"team"_s).isEmpty());
}

void TestGoogleSync::calendarListChangeIsSignalled()
{
    m_google->responses[u"/v3/users/me/calendarList"_s] = {{200, R"({"items":[]})"}};
    QSignalSpy changed(m_sync.get(), &GoogleSync::changed);

    QCOMPARE(runSync(), QStringList());

    QCOMPARE(changed.size(), 1);
}

void TestGoogleSync::outcomesAreRecorded()
{
    m_google->on(u"calendars/me%40example.com/events"_s, 500,
                 R"({"error":{"code":500,"message":"Backend Error"}})");
    m_google->on(u"calendars/team/events"_s, 200, events("", "team-1"));

    runSync();

    QVERIFY(m_cache->accountState(kAccount).lastSynced.isValid());
    QCOMPARE(m_cache->accountState(kAccount).lastError, QString());
    QCOMPARE(m_cache->calendarState(kAccount, kAccount.id).lastError, u"Backend Error"_s);
    QVERIFY(!m_cache->calendarState(kAccount, kAccount.id).lastSynced.isValid());
    QVERIFY(m_cache->calendarState(kAccount, u"team"_s).lastSynced.isValid());

    m_store->secrets.clear();
    m_tokens->invalidate(kAccount);
    const QStringList errors = runSync();

    QCOMPARE(errors.size(), 1);
    QCOMPARE(m_cache->accountState(kAccount).lastError, errors.first());
    QVERIFY(m_cache->accountState(kAccount).lastSynced.isValid());
}

QTEST_GUILESS_MAIN(TestGoogleSync)
#include "tst_googlesync.moc"
