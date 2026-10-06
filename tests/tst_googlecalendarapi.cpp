#include "FakeHttpServer.h"

#include "callie/GoogleCalendarApi.h"
#include "callie/QuickAdd.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTest>
#include <QTimeZone>

using namespace callie;

namespace {

QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(text).object();
}

} // namespace

class TestGoogleCalendarApi : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void ownResponseIsKept();
    void draftBecomesRequestBody();
    void insertPostsAndReturnsTheEvent();
    void init();

    void primaryCalendarIdSendsBearerToken();
    void errorCarriesStatusAndMessage();
    void calendarsFollowPages();
    void fullSyncSendsNoSyncToken();
    void incrementalSyncEncodesTokenAndCalendarId();
    void expiredSyncTokenIsReported();
    void missingSyncTokenFails();

    void timedEventKeepsOrganizerZone();
    void allDayEventHasDateOnly();
    void seriesAndExceptionAreParsed();
    void videoEntryPointWinsOverHangoutLink();

private:
    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<QNetworkAccessManager> m_network;
    std::unique_ptr<GoogleCalendarApi> m_api;

    GoogleEventChanges fetchEvents(const QString &calendarId, const QString &syncToken,
                                   GoogleApiError *error = nullptr);
};

void TestGoogleCalendarApi::init()
{
    m_server = std::make_unique<FakeHttpServer>();
    m_network = std::make_unique<QNetworkAccessManager>();
    m_api = std::make_unique<GoogleCalendarApi>(m_network.get());
    m_api->setBaseUrl(m_server->url(QStringLiteral("/calendar/v3/")));
}

GoogleEventChanges TestGoogleCalendarApi::fetchEvents(const QString &calendarId,
                                                      const QString &syncToken,
                                                      GoogleApiError *error)
{
    GoogleEventChanges changes;
    bool done = false;
    m_api->fetchEvents(QStringLiteral("at-1"), calendarId, syncToken,
                       [&](const GoogleEventChanges &c, const GoogleApiError &e) {
                           changes = c;
                           if (error)
                               *error = e;
                           done = true;
                       });
    [&] { QTRY_VERIFY_WITH_TIMEOUT(done, 5000); }();
    return changes;
}

void TestGoogleCalendarApi::primaryCalendarIdSendsBearerToken()
{
    m_server->respond(200, R"({"id":"me@example.com"})");

    QString id;
    GoogleApiError error;
    bool done = false;
    m_api->fetchPrimaryCalendarId(QStringLiteral("at-1"),
                                  [&](const QString &i, const GoogleApiError &e) {
                                      id = i;
                                      error = e;
                                      done = true;
                                  });

    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QVERIFY(!error);
    QCOMPARE(id, QStringLiteral("me@example.com"));
    QCOMPARE(m_server->requests.first().target,
             QByteArray("/calendar/v3/users/me/calendarList/primary"));
    QCOMPARE(m_server->requests.first().headers.value("authorization"), QByteArray("Bearer at-1"));
}

void TestGoogleCalendarApi::errorCarriesStatusAndMessage()
{
    m_server->respond(401, R"({"error":{"code":401,"message":"Invalid Credentials"}})");

    GoogleApiError error;
    bool done = false;
    m_api->fetchCalendars(QStringLiteral("expired"),
                          [&](const QList<GoogleCalendar> &, const GoogleApiError &e) {
                              error = e;
                              done = true;
                          });

    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QVERIFY(error.unauthorized());
    QCOMPARE(error.message, QStringLiteral("Invalid Credentials"));
}

void TestGoogleCalendarApi::calendarsFollowPages()
{
    m_server->enqueue(200, R"({"items":[{"id":"me@example.com","summary":"Me","primary":true,
                               "backgroundColor":"#9fe1e7","timeZone":"America/New_York",
                               "accessRole":"owner","selected":true}],
                               "nextPageToken":"p+2"})");
    m_server->enqueue(200, R"({"items":[{"id":"team@group.calendar.google.com",
                               "summary":"Team","summaryOverride":"Work",
                               "accessRole":"reader"}]})");

    QList<GoogleCalendar> calendars;
    GoogleApiError error;
    bool done = false;
    m_api->fetchCalendars(QStringLiteral("at-1"),
                          [&](const QList<GoogleCalendar> &c, const GoogleApiError &e) {
                              calendars = c;
                              error = e;
                              done = true;
                          });

    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QVERIFY(!error);
    QCOMPARE(m_server->requests.size(), 2);
    QCOMPARE(m_server->requests.at(1).target,
             QByteArray("/calendar/v3/users/me/calendarList?maxResults=250&pageToken=p%2B2"));
    QCOMPARE(calendars.size(), 2);
    QCOMPARE(calendars.at(0).id, QStringLiteral("me@example.com"));
    QVERIFY(calendars.at(0).primary);
    QVERIFY(calendars.at(0).selected);
    QCOMPARE(calendars.at(0).color, QStringLiteral("#9fe1e7"));
    QCOMPARE(calendars.at(0).timeZone, QStringLiteral("America/New_York"));
    QCOMPARE(calendars.at(1).summary, QStringLiteral("Work"));
    QCOMPARE(calendars.at(1).accessRole, QStringLiteral("reader"));
    QVERIFY(!calendars.at(1).primary);
}

void TestGoogleCalendarApi::fullSyncSendsNoSyncToken()
{
    m_server->enqueue(200, R"({"items":[{"id":"a","status":"confirmed"}],"nextPageToken":"p2"})");
    m_server->enqueue(200, R"({"items":[{"id":"b","status":"confirmed"}],"nextSyncToken":"s1"})");

    GoogleApiError error;
    const GoogleEventChanges changes = fetchEvents(QStringLiteral("me@example.com"), {}, &error);

    QVERIFY(!error);
    QCOMPARE(changes.nextSyncToken, QStringLiteral("s1"));
    QCOMPARE(changes.events.size(), 2);
    QCOMPARE(changes.events.at(1).id, QStringLiteral("b"));
    QCOMPARE(m_server->requests.at(0).target,
             QByteArray("/calendar/v3/calendars/me%40example.com/events?maxResults=2500"));
    QCOMPARE(m_server->requests.at(1).target,
             QByteArray("/calendar/v3/calendars/me%40example.com/events?maxResults=2500"
                        "&pageToken=p2"));
}

void TestGoogleCalendarApi::incrementalSyncEncodesTokenAndCalendarId()
{
    m_server->respond(200, R"({"items":[{"id":"a","status":"cancelled"}],"nextSyncToken":"s2"})");

    const GoogleEventChanges changes = fetchEvents(
        QStringLiteral("en.usa#holiday@group.v.calendar.google.com"), QStringLiteral("s1+/="));

    QCOMPARE(m_server->requests.first().target,
             QByteArray("/calendar/v3/calendars/en.usa%23holiday%40group.v.calendar.google.com"
                        "/events?maxResults=2500&syncToken=s1%2B%2F%3D"));
    QCOMPARE(changes.events.size(), 1);
    QVERIFY(changes.events.first().isCancelled());
    QCOMPARE(changes.nextSyncToken, QStringLiteral("s2"));
}

void TestGoogleCalendarApi::expiredSyncTokenIsReported()
{
    m_server->respond(410, R"({"error":{"code":410,"message":"Sync token is no longer valid",
                               "errors":[{"reason":"fullSyncRequired"}]}})");

    GoogleApiError error;
    fetchEvents(QStringLiteral("me@example.com"), QStringLiteral("old"), &error);

    QVERIFY(error.syncTokenExpired());
    QCOMPARE(error.message, QStringLiteral("Sync token is no longer valid"));
}

void TestGoogleCalendarApi::missingSyncTokenFails()
{
    m_server->respond(200, R"({"items":[]})");

    GoogleApiError error;
    fetchEvents(QStringLiteral("me@example.com"), {}, &error);

    QVERIFY(error);
    QVERIFY(!error.syncTokenExpired());
}

void TestGoogleCalendarApi::timedEventKeepsOrganizerZone()
{
    const GoogleEvent event = parseGoogleEvent(json(R"({
        "id":"e1","status":"confirmed","summary":"Standup","location":"Room 2",
        "start":{"dateTime":"2026-10-05T09:30:00-04:00","timeZone":"America/New_York"},
        "end":{"dateTime":"2026-10-05T09:45:00-04:00","timeZone":"America/New_York"},
        "updated":"2026-10-01T12:00:00.000Z"})"));

    QCOMPARE(event.summary, QStringLiteral("Standup"));
    QCOMPARE(event.location, QStringLiteral("Room 2"));
    QVERIFY(!event.start.isAllDay());
    QCOMPARE(event.start.dateTime.timeZone(), QTimeZone("America/New_York"));
    QCOMPARE(event.start.dateTime.time(), QTime(9, 30));
    QCOMPARE(event.start.dateTime.toUTC(),
             QDateTime(QDate(2026, 10, 5), QTime(13, 30), QTimeZone::UTC));
    QCOMPARE(event.end.dateTime.time(), QTime(9, 45));
    QCOMPARE(event.updated, QDateTime(QDate(2026, 10, 1), QTime(12, 0), QTimeZone::UTC));
    QVERIFY(!event.isCancelled());
}

void TestGoogleCalendarApi::allDayEventHasDateOnly()
{
    const GoogleEvent event = parseGoogleEvent(json(R"({
        "id":"e2","start":{"date":"2026-10-12"},"end":{"date":"2026-10-13"}})"));

    QVERIFY(event.start.isAllDay());
    QCOMPARE(event.start.date, QDate(2026, 10, 12));
    QCOMPARE(event.end.date, QDate(2026, 10, 13));
    QVERIFY(!event.start.dateTime.isValid());
}

void TestGoogleCalendarApi::seriesAndExceptionAreParsed()
{
    const GoogleEvent series = parseGoogleEvent(json(R"({
        "id":"s1","start":{"dateTime":"2026-10-05T10:00:00Z","timeZone":"Europe/Berlin"},
        "recurrence":["RRULE:FREQ=WEEKLY;BYDAY=MO","EXDATE;TZID=Europe/Berlin:20261012T120000"]})"));
    QCOMPARE(series.recurrence,
             QStringList({QStringLiteral("RRULE:FREQ=WEEKLY;BYDAY=MO"),
                          QStringLiteral("EXDATE;TZID=Europe/Berlin:20261012T120000")}));
    QCOMPARE(series.start.dateTime.time(), QTime(12, 0));

    const GoogleEvent exception = parseGoogleEvent(json(R"({
        "id":"s1_20261019T100000Z","recurringEventId":"s1",
        "originalStartTime":{"dateTime":"2026-10-19T12:00:00+02:00","timeZone":"Europe/Berlin"},
        "start":{"dateTime":"2026-10-19T14:00:00+02:00","timeZone":"Europe/Berlin"}})"));
    QCOMPARE(exception.recurringEventId, QStringLiteral("s1"));
    QCOMPARE(exception.originalStart.dateTime.toUTC(),
             QDateTime(QDate(2026, 10, 19), QTime(10, 0), QTimeZone::UTC));
}

void TestGoogleCalendarApi::videoEntryPointWinsOverHangoutLink()
{
    const GoogleEvent event = parseGoogleEvent(json(R"({
        "id":"e3","hangoutLink":"https://meet.google.com/aaa-bbbb-ccc",
        "conferenceData":{"entryPoints":[
            {"entryPointType":"phone","uri":"tel:+1-555-0100"},
            {"entryPointType":"video","uri":"https://example.zoom.us/j/123"}]}})"));
    QCOMPARE(event.conferenceUrl, QUrl(QStringLiteral("https://example.zoom.us/j/123")));

    const GoogleEvent meetOnly = parseGoogleEvent(
        json(R"({"id":"e4","hangoutLink":"https://meet.google.com/aaa-bbbb-ccc"})"));
    QCOMPARE(meetOnly.conferenceUrl, QUrl(QStringLiteral("https://meet.google.com/aaa-bbbb-ccc")));
}

void TestGoogleCalendarApi::ownResponseIsKept()
{
    const GoogleEvent event = parseGoogleEvent(json(R"({"id":"e5","attendees":[
        {"email":"boss@example.com","organizer":true,"responseStatus":"accepted"},
        {"email":"me@example.com","self":true,"responseStatus":"declined"}]})"));
    QCOMPARE(event.responseStatus, QStringLiteral("declined"));

    QVERIFY(parseGoogleEvent(json(R"({"id":"e6"})")).responseStatus.isEmpty());
}

void TestGoogleCalendarApi::draftBecomesRequestBody()
{
    const QTimeZone berlin("Europe/Berlin");
    EventDraft timed;
    timed.summary = QStringLiteral("Lunch");
    timed.location = QStringLiteral("Cafe Sol");
    timed.start = QDateTime(QDate(2026, 10, 8), QTime(12, 0), berlin);
    timed.end = timed.start.addSecs(3600);
    const QJsonObject json = googleEventJson(timed);
    QCOMPARE(json[u"summary"].toString(), QStringLiteral("Lunch"));
    QCOMPARE(json[u"location"].toString(), QStringLiteral("Cafe Sol"));
    QCOMPARE(json[u"start"][u"dateTime"].toString(), QStringLiteral("2026-10-08T12:00:00+02:00"));
    QCOMPARE(json[u"start"][u"timeZone"].toString(), QStringLiteral("Europe/Berlin"));
    QCOMPARE(json[u"end"][u"dateTime"].toString(), QStringLiteral("2026-10-08T13:00:00+02:00"));

    EventDraft allDay;
    allDay.summary = QStringLiteral("Holiday");
    allDay.allDay = true;
    allDay.start = QDateTime(QDate(2026, 10, 20), QTime(0, 0), berlin);
    allDay.end = allDay.start.addDays(1);
    const QJsonObject day = googleEventJson(allDay);
    QCOMPARE(day[u"start"][u"date"].toString(), QStringLiteral("2026-10-20"));
    QCOMPARE(day[u"end"][u"date"].toString(), QStringLiteral("2026-10-21"));
    QVERIFY(!day.contains(u"location"));
}

void TestGoogleCalendarApi::insertPostsAndReturnsTheEvent()
{
    m_server->respond(200, R"({"id":"new1","status":"confirmed","summary":"Lunch",
        "start":{"dateTime":"2026-10-08T12:00:00+02:00"},
        "end":{"dateTime":"2026-10-08T13:00:00+02:00"}})");

    GoogleEvent created;
    GoogleApiError error;
    bool done = false;
    m_api->insertEvent(QStringLiteral("at-1"), QStringLiteral("team@group.calendar.google.com"),
                       QJsonObject{{QStringLiteral("summary"), QStringLiteral("Lunch")}},
                       [&](const GoogleEvent &event, const GoogleApiError &e) {
                           created = event;
                           error = e;
                           done = true;
                       });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

    QVERIFY(!error);
    QCOMPARE(created.id, QStringLiteral("new1"));
    const FakeHttpServer::Request &request = m_server->requests.last();
    QCOMPARE(request.method, QByteArray("POST"));
    QVERIFY(request.target.contains("calendars/team%40group.calendar.google.com/events"));
    QCOMPARE(request.headers.value("authorization"), QByteArray("Bearer at-1"));
    QVERIFY(request.body.contains("\"summary\":\"Lunch\""));
}

QTEST_GUILESS_MAIN(TestGoogleCalendarApi)
#include "tst_googlecalendarapi.moc"
