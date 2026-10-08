#include "FakeHttpServer.h"
#include "FakeTokenStore.h"

#include "callie/GoogleCache.h"
#include "callie/GoogleCalendarApi.h"
#include "callie/GoogleRecurrence.h"
#include "callie/GoogleSync.h"
#include "callie/GoogleTokenProvider.h"
#include "callie/QuickAdd.h"

#include <QJsonDocument>
#include <QJsonObject>
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
    void createdEventIsStoredWithoutCountingAsASync();
    void createRetriesOnceAfterRejection();
    void createReportsWhatGoogleSaid();
    void createWithTakenIdIsAlreadyMade();
    void answerKeepsOtherGuestsIntact();
    void answerNeedsAnInvitation();
    void deletingAnOccurrenceKeepsTheRest();
    void deletingASeriesRemovesIt();
    void missingRefreshTokenIsReported();
    void concurrentSyncsShareOneRun();
    void removedCalendarIsDropped();
    void calendarListChangeIsSignalled();
    void outcomesAreRecorded();
    void forgottenAccountStoresNothing();
    void settingsAreReadOnceARun();
    void contactsAreReadOnceARun();
    void contactsOfARemovedAccountAreDropped();
    void unreadableSettingsAreTriedAgain();
    void rejectedTokenForSettingsIsRefreshed();

private:
    /// The last request about calendars, since a sync goes on to read contacts.
    [[nodiscard]] const FakeHttpServer::Request &lastCalendarRequest() const
    {
        const auto &requests = m_apiServer->requests;
        return *std::find_if(requests.crbegin(), requests.crend(), [](const auto &r) {
            return !r.target.contains("people") && !r.target.contains("otherContacts");
        });
    }
    QStringList runSync();
    QString create(const QString &calendarId);
    void syncOnce();
    QString act(const std::function<void(GoogleSync::Created)> &action);
    void storeSeries();

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
    m_api->setPeopleBaseUrl(m_apiServer->url(u"/v3/"_s));
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

void TestGoogleSync::syncOnce()
{
    m_google->on(u"users/me/calendarList"_s, 200, kCalendarList);
    m_google->on(u"calendars/me%40example.com/events"_s, 200, events("", "me-1"));
    m_google->on(u"calendars/team/events"_s, 200, events("", "team-1"));
    QCOMPARE(runSync(), QStringList());
}

void TestGoogleSync::settingsAreReadOnceARun()
{
    m_google->on(u"calendars/me%40example.com/events"_s, 200, events("", "me-1"));
    m_google->on(u"calendars/team/events"_s, 200, events("", "team-1"));
    m_google->on(u"users/me/settings"_s, 200,
                 R"({"items":[{"id":"weekStart","value":"0"}],"nextPageToken":"p2"})");
    m_google->on(u"users/me/settings"_s, 200,
                 R"({"items":[{"id":"hideWeekends","value":"true"}]})");
    QSignalSpy found(m_sync.get(), &GoogleSync::settingsFound);

    QCOMPARE(runSync(), QStringList());
    QTRY_COMPARE_WITH_TIMEOUT(found.size(), 1, 5000);
    const auto settings = found.first().at(1).value<QHash<QString, QString>>();
    QCOMPARE(settings.value(u"weekStart"_s), u"0"_s);
    QCOMPARE(settings.value(u"hideWeekends"_s), u"true"_s);

    QCOMPARE(runSync(), QStringList());
    QTest::qWait(200);
    QCOMPARE(found.size(), 1);
    QCOMPARE(m_google->count(u"users/me/settings"_s), 2);
}

void TestGoogleSync::contactsAreReadOnceARun()
{
    m_google->on(u"calendars/me%40example.com/events"_s, 200, events("", "me-1"));
    m_google->on(u"calendars/team/events"_s, 200, events("", "team-1"));
    m_google->on(u"people/me/connections"_s, 200,
                 R"({"connections":[{"names":[{"displayName":"Priya Shah"}],
                     "emailAddresses":[{"value":"priya@example.com"},{"value":"p@home.example"}]}],
                     "nextPageToken":"c2"})");
    m_google->on(u"people/me/connections"_s, 200,
                 R"({"connections":[{"emailAddresses":[{"value":"lee@example.com"}]}]})");
    m_google->on(u"otherContacts"_s, 200,
                 R"({"otherContacts":[{"emailAddresses":[{"value":"vendor@example.org"}]}]})");
    // A personal account has no directory.
    m_google->on(u"people:listDirectoryPeople"_s, 403,
                 R"({"error":{"code":403,"message":"Must be a G Suite domain user."}})");
    QSignalSpy found(m_sync.get(), &GoogleSync::contactsFound);

    QCOMPARE(runSync(), QStringList());
    QTRY_COMPARE_WITH_TIMEOUT(found.size(), 1, 5000);
    const auto people = found.first().at(1).value<QList<Contact>>();
    QCOMPARE(people, (QList<Contact>{{u"Priya Shah"_s, u"priya@example.com"_s},
                                     {u"Priya Shah"_s, u"p@home.example"_s},
                                     {{}, u"lee@example.com"_s},
                                     {{}, u"vendor@example.org"_s}}));
    const auto asked =
        std::find_if(m_google->requests.cbegin(), m_google->requests.cend(),
                     [](const auto &r) { return r.first.endsWith(u"otherContacts"_s); });
    QCOMPARE(asked->second.queryItemValue(u"readMask"_s, QUrl::FullyDecoded),
             u"names,emailAddresses"_s);

    // Once a run is enough.
    QCOMPARE(runSync(), QStringList());
    QTest::qWait(200);
    QCOMPARE(found.size(), 1);
    QCOMPARE(m_google->count(u"otherContacts"_s), 1);
}

void TestGoogleSync::contactsOfARemovedAccountAreDropped()
{
    m_google->on(u"calendars/me%40example.com/events"_s, 200, events("", "me-1"));
    m_google->on(u"calendars/team/events"_s, 200, events("", "team-1"));
    m_google->on(u"people/me/connections"_s, 200,
                 R"({"connections":[{"emailAddresses":[{"value":"lee@example.com"}]}]})");
    m_google->on(u"otherContacts"_s, 200, R"({})");
    m_google->on(u"people:listDirectoryPeople"_s, 200, R"({})");
    // The account is removed while its contacts are still being read.
    const auto answer = m_apiServer->handler;
    m_apiServer->handler = [this, answer](const FakeHttpServer::Request &request) {
        if (request.target.contains("listDirectoryPeople"))
            m_sync->forget(kAccount);
        return answer(request);
    };
    QSignalSpy found(m_sync.get(), &GoogleSync::contactsFound);

    QCOMPARE(runSync(), QStringList());
    QTRY_COMPARE_WITH_TIMEOUT(m_google->count(u"people:listDirectoryPeople"_s), 1, 5000);
    QTest::qWait(300);
    QVERIFY(found.isEmpty());
}

void TestGoogleSync::unreadableSettingsAreTriedAgain()
{
    // Signed in before Callie asked to read settings: Google says no.
    m_google->on(u"calendars/me%40example.com/events"_s, 200, events("", "me-1"));
    m_google->on(u"calendars/team/events"_s, 200, events("", "team-1"));
    m_google->on(
        u"users/me/settings"_s, 403,
        R"({"error":{"code":403,"message":"Request had insufficient authentication scopes."}})");
    QSignalSpy found(m_sync.get(), &GoogleSync::settingsFound);

    QCOMPARE(runSync(), QStringList());
    QTRY_COMPARE_WITH_TIMEOUT(m_google->count(u"users/me/settings"_s), 1, 5000);
    QTest::qWait(200);
    QVERIFY(found.isEmpty());
    // The account still counts as synced.
    QVERIFY(m_cache->accountState(kAccount).lastSynced.isValid());

    // Signed in again with the permission, the next sync reads them.
    m_google->responses[u"/v3/users/me/settings"_s] = {
        {200, R"({"items":[{"id":"weekStart","value":"1"}]})"}};
    QCOMPARE(runSync(), QStringList());
    QTRY_COMPARE_WITH_TIMEOUT(found.size(), 1, 5000);
}

void TestGoogleSync::rejectedTokenForSettingsIsRefreshed()
{
    m_google->on(u"calendars/me%40example.com/events"_s, 200, events("", "me-1"));
    m_google->on(u"calendars/team/events"_s, 200, events("", "team-1"));
    m_google->on(u"users/me/settings"_s, 401, R"({"error":{"code":401,"message":"expired"}})");
    m_google->on(u"users/me/settings"_s, 200, R"({"items":[{"id":"weekStart","value":"6"}]})");
    QSignalSpy found(m_sync.get(), &GoogleSync::settingsFound);

    QCOMPARE(runSync(), QStringList());
    QTRY_COMPARE_WITH_TIMEOUT(found.size(), 1, 5000);
    QCOMPARE(m_google->count(u"users/me/settings"_s), 2);
    const auto settings = found.first().at(1).value<QHash<QString, QString>>();
    QCOMPARE(settings.value(u"weekStart"_s), u"6"_s);
}

void TestGoogleSync::forgottenAccountStoresNothing()
{
    m_google->on(u"calendars/me%40example.com/events"_s, 200, events(R"({"id":"a"})", "me-1"));
    m_google->on(u"calendars/team/events"_s, 200, events(R"({"id":"t"})", "team-1"));
    QStringList errors{u"not called"_s};
    m_sync->sync(kAccount, [&](const QStringList &e) { errors = e; });
    // Removed while the first request is still on its way.
    m_sync->forget(kAccount);
    QTRY_VERIFY_WITH_TIMEOUT(errors.isEmpty(), 5000);

    QVERIFY(m_cache->calendars(kAccount).isEmpty());
    QVERIFY(m_cache->events(kAccount, kAccount.id).isEmpty());
    QVERIFY(!m_cache->accountState(kAccount).lastSynced.isValid());

    // A later sync for the same account starts afresh.
    QCOMPARE(runSync(), QStringList());
    QCOMPARE(m_cache->calendars(kAccount).size(), 2);
}

QString TestGoogleSync::create(const QString &calendarId)
{
    EventDraft draft;
    draft.summary = u"Pottery"_s;
    draft.start = QDateTime(QDate(2026, 10, 9), QTime(18, 0), QTimeZone::UTC);
    draft.end = draft.start.addSecs(7200);
    QString result = u"unset"_s;
    m_sync->createEvent(kAccount, calendarId, draft, [&result](const QString &e) { result = e; });
    [&] { QTRY_VERIFY_WITH_TIMEOUT(result != u"unset", 5000); }();
    return result;
}

namespace {
const char *const kCreated = R"({"id":"made","status":"confirmed","summary":"Pottery",
    "start":{"dateTime":"2026-10-09T18:00:00Z"},"end":{"dateTime":"2026-10-09T20:00:00Z"}})";
}

void TestGoogleSync::createdEventIsStoredWithoutCountingAsASync()
{
    syncOnce();
    QVERIFY(m_cache->recordCalendarError(kAccount, u"team"_s, u"quota"_s));
    const SyncState before = m_cache->calendarState(kAccount, u"team"_s);
    const QString token = m_cache->syncToken(kAccount, u"team"_s);
    m_google->responses.clear();
    m_google->on(u"calendars/team/events"_s, 200, kCreated);
    QSignalSpy changed(m_sync.get(), &GoogleSync::changed);

    QCOMPARE(create(u"team"_s), QString());

    QCOMPARE(changed.size(), 1);
    const QList<GoogleEvent> stored = m_cache->events(kAccount, u"team"_s);
    QVERIFY(std::any_of(stored.cbegin(), stored.cend(),
                        [](const GoogleEvent &e) { return e.id == u"made"; }));
    // The calendar did not sync, so its status and token are as they were.
    const SyncState after = m_cache->calendarState(kAccount, u"team"_s);
    QCOMPARE(after.lastError, u"quota"_s);
    QCOMPARE(after.lastSynced, before.lastSynced);
    QCOMPARE(m_cache->syncToken(kAccount, u"team"_s), token);
}

void TestGoogleSync::createRetriesOnceAfterRejection()
{
    syncOnce();
    m_google->responses.clear();
    m_google->on(u"calendars/team/events"_s, 401, R"({"error":{"message":"Invalid Credentials"}})");
    m_google->on(u"calendars/team/events"_s, 200, kCreated);
    const qsizetype tokensBefore = m_tokenServer->requests.size();

    QCOMPARE(create(u"team"_s), QString());
    QCOMPARE(m_tokenServer->requests.size(), tokensBefore + 1);
    // One sync, then the rejected and the accepted creation.
    QCOMPARE(m_google->count(u"calendars/team/events"_s), 3);
}

void TestGoogleSync::createReportsWhatGoogleSaid()
{
    syncOnce();
    m_google->responses.clear();
    m_google->on(u"calendars/team/events"_s, 403,
                 R"({"error":{"message":"You need writer access"}})");
    const qsizetype storedBefore = m_cache->events(kAccount, u"team"_s).size();

    const QString error = create(u"team"_s);

    QVERIFY(error.contains(u"You need writer access"_s));
    QCOMPARE(m_cache->events(kAccount, u"team"_s).size(), storedBefore);
}

void TestGoogleSync::createWithTakenIdIsAlreadyMade()
{
    syncOnce();
    m_google->responses.clear();
    // An earlier try made it, but its answer was lost: the id is taken.
    m_google->on(u"calendars/team/events"_s, 409,
                 R"({"error":{"message":"The requested identifier already exists."}})");
    EventDraft draft;
    draft.summary = u"Pottery"_s;
    draft.id = u"0123456789abcdefuv"_s;
    draft.start = QDateTime(QDate(2026, 10, 9), QTime(18, 0), QTimeZone::UTC);
    draft.end = draft.start.addSecs(7200);
    QString result = u"unset"_s;
    m_sync->createEvent(kAccount, u"team"_s, draft, [&result](const QString &e) { result = e; });
    QTRY_VERIFY_WITH_TIMEOUT(result != u"unset", 5000);
    QCOMPARE(result, QString());
    const QJsonObject sent = QJsonDocument::fromJson(lastCalendarRequest().body).object();
    QCOMPARE(sent[u"id"].toString(), u"0123456789abcdefuv"_s);
}

QString TestGoogleSync::act(const std::function<void(GoogleSync::Created)> &action)
{
    QString result = u"unset"_s;
    action([&result](const QString &e) { result = e; });
    [&] { QTRY_VERIFY_WITH_TIMEOUT(result != u"unset", 5000); }();
    return result;
}

void TestGoogleSync::storeSeries()
{
    syncOnce();
    QVERIFY(m_cache->storeEvents(
        kAccount, u"team"_s,
        {parseGoogleEvent(QJsonDocument::fromJson(R"({"id":"weekly","summary":"Weekly",
            "start":{"dateTime":"2026-10-05T10:00:00Z"},"end":{"dateTime":"2026-10-05T11:00:00Z"},
            "recurrence":["RRULE:FREQ=WEEKLY"],
            "attendees":[{"email":"boss@example.com","organizer":true,"comment":"keep me"},
                         {"email":"me@example.com","self":true,"responseStatus":"needsAction"}]})")
                              .object())}));
    m_google->responses.clear();
}

namespace {
GoogleSync::Target occurrence()
{
    GoogleSync::Target target;
    target.calendarId = u"team"_s;
    target.eventId = u"weekly_20261012T100000Z"_s;
    target.seriesId = u"weekly"_s;
    target.originalStart.dateTime = QDateTime(QDate(2026, 10, 12), QTime(10, 0), QTimeZone::UTC);
    return target;
}

QList<Event> week(GoogleCache &cache, const Account &account)
{
    const QDateTime from(QDate(2026, 10, 5), QTime(0, 0), QTimeZone::UTC);
    return expandGoogleEvents(cache.events(account, u"team"_s), from, from.addDays(14),
                              QTimeZone::UTC);
}
} // namespace

void TestGoogleSync::answerKeepsOtherGuestsIntact()
{
    storeSeries();
    m_google->on(u"calendars/team/events/weekly_20261012T100000Z"_s, 200,
                 R"({"id":"weekly_20261012T100000Z","recurringEventId":"weekly",
                     "status":"confirmed","summary":"Weekly",
                     "originalStartTime":{"dateTime":"2026-10-12T10:00:00Z"},
                     "start":{"dateTime":"2026-10-12T10:00:00Z"},
                     "end":{"dateTime":"2026-10-12T11:00:00Z"},
                     "attendees":[{"email":"me@example.com","self":true,
                                   "responseStatus":"accepted"}]})");

    QCOMPARE(act([&](GoogleSync::Created done) {
                 m_sync->respond(kAccount, occurrence(), u"accepted"_s, done);
             }),
             QString());

    // Only our own answer changed; the organizer's entry went back as it was.
    const QByteArray body = lastCalendarRequest().body;
    QVERIFY(body.contains(R"("responseStatus":"accepted")"));
    QVERIFY(body.contains(R"("comment":"keep me")"));
    const QList<Event> events = week(*m_cache, kAccount);
    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(0).responseStatus, u"needsAction"_s);
    QCOMPARE(events.at(1).responseStatus, u"accepted"_s);
}

void TestGoogleSync::answerNeedsAnInvitation()
{
    syncOnce();
    GoogleSync::Target target;
    target.calendarId = u"team"_s;
    target.eventId = u"nothing"_s;
    QVERIFY(!act([&](GoogleSync::Created done) {
                 m_sync->respond(kAccount, target, u"accepted"_s, done);
             }).isEmpty());
}

void TestGoogleSync::deletingAnOccurrenceKeepsTheRest()
{
    storeSeries();
    m_google->on(u"calendars/team/events/weekly_20261012T100000Z"_s, 204, "");

    QCOMPARE(act([&](GoogleSync::Created done) { m_sync->remove(kAccount, occurrence(), done); }),
             QString());

    QCOMPARE(lastCalendarRequest().method, QByteArray("DELETE"));
    const QList<Event> events = week(*m_cache, kAccount);
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.first().start.date(), QDate(2026, 10, 5));
}

void TestGoogleSync::deletingASeriesRemovesIt()
{
    storeSeries();
    m_google->on(u"calendars/team/events/weekly"_s, 204, "");
    GoogleSync::Target whole = occurrence();
    whole.eventId = whole.seriesId;

    QCOMPARE(act([&](GoogleSync::Created done) { m_sync->remove(kAccount, whole, done); }),
             QString());
    QVERIFY(week(*m_cache, kAccount).isEmpty());
}

QTEST_GUILESS_MAIN(TestGoogleSync)
#include "tst_googlesync.moc"
