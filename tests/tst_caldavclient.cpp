#include "FakeCalDavServer.h"

#include "callie/CalDavClient.h"

#include <QNetworkAccessManager>
#include <QTest>

#include <optional>

using namespace callie;
using namespace Qt::StringLiterals;

class TestCalDavClient : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void discoveryFindsTheCalendars();
    void wrongPasswordsSaySo();
    void changesSinceAToken();
    void listsWhereThereAreNoTokens();
    void fetchesInBatches();
    void writesOnlyOverWhatWasRead();
    void thePasswordGoesOnlyWhereItShould();

private:
    std::unique_ptr<FakeCalDavServer> m_server;
    std::unique_ptr<QNetworkAccessManager> m_network;
    std::unique_ptr<CalDavClient> m_client;
};

namespace {

const QByteArray kLunch = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//t//EN\r\nBEGIN:VEVENT\r\n"
                          "UID:lunch\r\nDTSTART:20261006T120000Z\r\nDTEND:20261006T130000Z\r\n"
                          "SUMMARY:Lunch & learn\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

/// Waits for a callback's answer.
template<typename T>
T wait(const std::function<void(std::function<void(T)>)> &call)
{
    std::optional<T> answer;
    call([&answer](T value) { answer = std::move(value); });
    if (!QTest::qWaitFor([&answer] { return answer.has_value(); }, 5000))
        qWarning() << "no answer in time";
    return answer.value_or(T());
}

} // namespace

void TestCalDavClient::init()
{
    m_server = std::make_unique<FakeCalDavServer>();
    m_server->addCalendar("work", "Work");
    m_server->addCalendar("shared", "Shared", false);
    m_network = std::make_unique<QNetworkAccessManager>();
    m_client = std::make_unique<CalDavClient>(m_network.get());
    m_client->setCredentials(u"me"_s, u"app-password"_s);
}

void TestCalDavClient::discoveryFindsTheCalendars()
{
    using Found = std::pair<DavAccount, DavError>;
    // From the server's address alone, by way of /.well-known/caldav.
    const Found found = wait<Found>([&](auto done) {
        m_client->discover(m_server->url(),
                           [done](const DavAccount &a, const DavError &e) { done({a, e}); });
    });
    QVERIFY2(!found.second, qPrintable(found.second.message));
    QCOMPARE(found.first.home, m_server->url(u"/dav/calendars/me/"_s));
    QCOMPARE(found.first.addresses, QStringList{u"me@example.com"_s});
    QCOMPARE(found.first.calendars.size(), 2);
    const DavCalendar shared = found.first.calendars.at(0);
    const DavCalendar work = found.first.calendars.at(1);
    QCOMPARE(work.displayName, u"Work"_s);
    QCOMPARE(work.color, QColor(u"#33b679"_s));
    QVERIFY(work.writable);
    QVERIFY(!shared.writable);
    QVERIFY(!work.syncToken.isEmpty());
}

void TestCalDavClient::wrongPasswordsSaySo()
{
    m_client->setCredentials(u"me"_s, u"guess"_s);
    using Found = std::pair<DavAccount, DavError>;
    const Found found = wait<Found>([&](auto done) {
        m_client->discover(m_server->url(),
                           [done](const DavAccount &a, const DavError &e) { done({a, e}); });
    });
    QVERIFY(found.second.isAuth());
    QVERIFY(found.second.message.contains(u"app password"_s));
}

void TestCalDavClient::changesSinceAToken()
{
    const QUrl work = m_server->url(QString::fromLatin1(FakeCalDavServer::calendarPath("work")));
    m_server->putElsewhere("work", "lunch.ics", kLunch);
    using Got = std::pair<CalDavClient::Changes, DavError>;
    const Got first = wait<Got>([&](auto done) {
        m_client->changes(work, {}, [done](const auto &c, const DavError &e) { done({c, e}); });
    });
    QVERIFY(!first.second);
    QCOMPARE(first.first.changed.size(), 1);
    QCOMPARE(first.first.changed.first().href,
             m_server->url(u"/dav/calendars/me/work/lunch.ics"_s));

    m_server->putElsewhere("work", "dinner.ics", kLunch);
    m_server->removeElsewhere("work", "lunch.ics");
    const Got next = wait<Got>([&](auto done) {
        m_client->changes(work, first.first.token,
                          [done](const auto &c, const DavError &e) { done({c, e}); });
    });
    QCOMPARE(next.first.changed.size(), 1);
    QCOMPARE(next.first.removed.size(), 1);
    QVERIFY(next.first.token != first.first.token);

    // A token the server has forgotten.
    const Got stale = wait<Got>([&](auto done) {
        m_client->changes(work, u"https://example.com/sync/999"_s,
                          [done](const auto &c, const DavError &e) { done({c, e}); });
    });
    QCOMPARE(stale.second.status, 403);
}

void TestCalDavClient::listsWhereThereAreNoTokens()
{
    const QUrl work = m_server->url(QString::fromLatin1(FakeCalDavServer::calendarPath("work")));
    m_server->putElsewhere("work", "lunch.ics", kLunch);
    using Got = std::pair<QList<DavItem>, DavError>;
    const Got listed = wait<Got>([&](auto done) {
        m_client->list(work, [done](const auto &items, const DavError &e) { done({items, e}); });
    });
    QCOMPARE(listed.first.size(), 1);
    QVERIFY(!listed.first.first().etag.isEmpty());
}

void TestCalDavClient::fetchesInBatches()
{
    const QUrl work = m_server->url(QString::fromLatin1(FakeCalDavServer::calendarPath("work")));
    QList<QUrl> hrefs;
    for (int i = 0; i < 120; ++i) {
        const QByteArray name = "e" + QByteArray::number(i) + ".ics";
        m_server->putElsewhere("work", name, kLunch);
        hrefs << m_server->url(u"/dav/calendars/me/work/"_s + QString::fromLatin1(name));
    }
    using Got = std::pair<QList<DavItem>, DavError>;
    const Got got = wait<Got>([&](auto done) {
        m_client->fetch(work, hrefs,
                        [done](const auto &items, const DavError &e) { done({items, e}); });
    });
    QVERIFY(!got.second);
    QCOMPARE(got.first.size(), 120);
    QVERIFY(got.first.first().data.contains("SUMMARY:Lunch & learn"));
    qsizetype reports = 0;
    for (const auto &request : m_server->server.requests)
        reports += request.method == "REPORT" ? 1 : 0;
    QCOMPARE(reports, 3);
}

void TestCalDavClient::writesOnlyOverWhatWasRead()
{
    const QUrl lunch = m_server->url(u"/dav/calendars/me/work/lunch.ics"_s);
    using Put = std::pair<QString, DavError>;
    const Put made = wait<Put>([&](auto done) {
        m_client->put(lunch, kLunch, {},
                      [done](const QString &etag, const DavError &e) { done({etag, e}); });
    });
    QVERIFY(!made.second);
    QVERIFY(!made.first.isEmpty());

    // Made twice, the second finds the first there.
    const Put again = wait<Put>([&](auto done) {
        m_client->put(lunch, kLunch, {},
                      [done](const QString &etag, const DavError &e) { done({etag, e}); });
    });
    QVERIFY(again.second.isConflict());

    const Put changed = wait<Put>([&](auto done) {
        m_client->put(lunch, kLunch, made.first,
                      [done](const QString &etag, const DavError &e) { done({etag, e}); });
    });
    QVERIFY(!changed.second);
    // The version read before the change is out of date now.
    const DavError stale = wait<DavError>([&](auto done) {
        m_client->remove(lunch, made.first, [done](const DavError &e) { done(e); });
    });
    QVERIFY(stale.isConflict());
    QVERIFY(!wait<DavError>([&](auto done) {
        m_client->remove(lunch, changed.first, [done](const DavError &e) { done(e); });
    }));
    // Gone already counts as gone.
    QVERIFY(!wait<DavError>(
        [&](auto done) { m_client->remove(lunch, {}, [done](const DavError &e) { done(e); }); }));

    // A calendar the user may only read.
    const Put refused = wait<Put>([&](auto done) {
        m_client->put(m_server->url(u"/dav/calendars/me/shared/x.ics"_s), kLunch, {},
                      [done](const QString &etag, const DavError &e) { done({etag, e}); });
    });
    QCOMPARE(refused.second.status, 403);
}

void TestCalDavClient::thePasswordGoesOnlyWhereItShould()
{
    using Found = std::pair<DavAccount, DavError>;
    const auto discover = [this](const QUrl &url) {
        return wait<Found>([&](auto done) {
            m_client->discover(url,
                               [done](const DavAccount &a, const DavError &e) { done({a, e}); });
        });
    };
    // "localhost" is the same server under another name, which a redirect
    // there does not earn the password.
    const QUrl elsewhere(u"http://localhost:%1/dav/"_s.arg(m_server->url().port()));
    m_server->redirects.insert("/hop", elsewhere.toEncoded());
    const Found hopped = discover(m_server->url(u"/hop"_s));
    QVERIFY(hopped.second.isAuth());
    // Not blamed on the password, which never went there.
    QVERIFY2(hopped.second.message.contains(u"not trust"_s), qPrintable(hopped.second.message));

    // Nor does a host named by an answer from somewhere a redirect led.
    const QByteArray other = "http://localhost:" + QByteArray::number(m_server->url().port());
    m_server->redirects.insert("/hop2", other + "/open/");
    m_server->anonymous.insert("/open/",
                               "<d:response><d:href>/open/</d:href><d:propstat><d:prop>"
                               "<d:current-user-principal><d:href>" +
                                   other +
                                   "/dav/principals/me/</d:href>"
                                   "</d:current-user-principal></d:prop><d:status>"
                                   "HTTP/1.1 200 OK</d:status></d:propstat></d:response>");
    QVERIFY(discover(m_server->url(u"/hop2"_s)).second.isAuth());

    // A host the server names itself, as iCloud names each account's, does.
    m_server->homeHref =
        u"http://localhost:%1/dav/calendars/me/"_s.arg(m_server->url().port()).toUtf8();
    const Found named = discover(m_server->url());
    QVERIFY2(!named.second, qPrintable(named.second.message));
    QCOMPARE(named.first.home.host(), u"localhost"_s);
    QCOMPARE(named.first.calendars.size(), 2);

    // Never over plain http to anywhere but this machine.
    const qsizetype before = m_server->server.requests.size();
    using Listed = std::pair<QList<DavCalendar>, DavError>;
    const Listed refused = wait<Listed>([&](auto done) {
        m_client->calendars(QUrl(u"http://calendar.example/dav/"_s),
                            [done](const auto &c, const DavError &e) { done({c, e}); });
    });
    QVERIFY(refused.second.message.contains(u"https"_s));
    QCOMPARE(m_server->server.requests.size(), before);

    // Nor from https down to http.
    QVERIFY(
        !CalDavClient::mayFollow(QUrl(u"https://dav.example/"_s), QUrl(u"http://dav.example/"_s)));
    QVERIFY(CalDavClient::mayFollow(QUrl(u"https://dav.example/"_s),
                                    QUrl(u"https://other.example/"_s)));
    QVERIFY(CalDavClient::mayFollow(QUrl(u"http://127.0.0.1/"_s), QUrl(u"http://localhost/"_s)));

    // Nor around in circles.
    m_server->redirects.insert("/loop", "/loop");
    const Listed circled = wait<Listed>([&](auto done) {
        m_client->calendars(m_server->url(u"/loop"_s),
                            [done](const auto &c, const DavError &e) { done({c, e}); });
    });
    QVERIFY(circled.second.message.contains(u"too often"_s));
}

QTEST_MAIN(TestCalDavClient)
#include "tst_caldavclient.moc"
