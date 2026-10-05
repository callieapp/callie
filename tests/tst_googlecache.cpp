#include "callie/GoogleCache.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

const Account kAccount{QStringLiteral("google"), QStringLiteral("me@example.com")};
const Account kOther{QStringLiteral("google"), QStringLiteral("other@example.com")};
const QString kCalendar = QStringLiteral("me@example.com");

GoogleEvent parsed(const char *json)
{
    return parseGoogleEvent(QJsonDocument::fromJson(json).object());
}

GoogleCalendar calendar(const QString &id)
{
    GoogleCalendar result;
    result.id = id;
    result.summary = id;
    return result;
}

QStringList ids(const QList<GoogleEvent> &events)
{
    QStringList result;
    for (const GoogleEvent &e : events)
        result.append(e.id);
    result.sort();
    return result;
}

} // namespace

class TestGoogleCache : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();

    void newCalendarHasNoSyncToken();
    void eventsRoundTrip();
    void fullSyncReplacesEvents();
    void deletedEventIsRemoved();
    void cancelledOccurrenceIsKept();
    void deletedSeriesTakesExceptions();
    void syncTokenIsStoredWithChanges();
    void unknownCalendarIsRejected();
    void removedCalendarLosesEvents();
    void keptCalendarKeepsToken();
    void accountsAreSeparate();
    void dataSurvivesReopen();
    void outdatedSchemaIsRebuilt();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<GoogleCache> m_cache;

    QString path() const { return m_dir->filePath(QStringLiteral("cache/google.sqlite")); }
    void apply(const QList<GoogleEvent> &events, bool full, const QString &token = u"t"_s);
};

void TestGoogleCache::init()
{
    m_cache.reset();
    m_dir = std::make_unique<QTemporaryDir>();
    m_cache = std::make_unique<GoogleCache>(path());
    QVERIFY2(m_cache->open(), qPrintable(m_cache->errorString()));
    QVERIFY(m_cache->setCalendars(kAccount, {calendar(kCalendar)}));
}

void TestGoogleCache::apply(const QList<GoogleEvent> &events, bool full, const QString &token)
{
    QVERIFY2(m_cache->applyChanges(kAccount, kCalendar, {events, token}, full),
             qPrintable(m_cache->errorString()));
}

void TestGoogleCache::newCalendarHasNoSyncToken()
{
    QCOMPARE(m_cache->syncToken(kAccount, kCalendar), QString());
    QCOMPARE(m_cache->calendars(kAccount).size(), 1);
    QVERIFY(m_cache->events(kAccount, kCalendar).isEmpty());
}

void TestGoogleCache::eventsRoundTrip()
{
    apply({parsed(R"({"id":"timed","status":"confirmed","summary":"Standup","location":"Room 2",
                    "description":"notes","hangoutLink":"https://meet.google.com/abc",
                    "start":{"dateTime":"2026-10-05T09:30:00-04:00","timeZone":"America/New_York"},
                    "end":{"dateTime":"2026-10-05T09:45:00-04:00","timeZone":"America/New_York"},
                    "recurrence":["RRULE:FREQ=WEEKLY;BYDAY=MO","EXDATE:20261012T133000Z"],
                    "updated":"2026-10-01T12:00:00.000Z"})"),
           parsed(R"({"id":"allday","start":{"date":"2026-10-12"},"end":{"date":"2026-10-13"}})"),
           parsed(R"({"id":"timed_20261019T133000Z","recurringEventId":"timed",
                    "originalStartTime":{"dateTime":"2026-10-19T09:30:00-04:00",
                                         "timeZone":"America/New_York"},
                    "start":{"dateTime":"2026-10-19T11:00:00-04:00"},
                    "end":{"dateTime":"2026-10-19T11:15:00-04:00"}})")},
          true);

    const QList<GoogleEvent> stored = m_cache->events(kAccount, kCalendar);
    QCOMPARE(stored.size(), 3);

    const GoogleEvent &timed = stored.at(0);
    QCOMPARE(timed.summary, QStringLiteral("Standup"));
    QCOMPARE(timed.location, QStringLiteral("Room 2"));
    QCOMPARE(timed.description, QStringLiteral("notes"));
    QCOMPARE(timed.conferenceUrl, QUrl(QStringLiteral("https://meet.google.com/abc")));
    QCOMPARE(timed.start.dateTime.timeZone(), QTimeZone("America/New_York"));
    QCOMPARE(timed.start.dateTime.time(), QTime(9, 30));
    QCOMPARE(timed.end.dateTime.time(), QTime(9, 45));
    QCOMPARE(timed.recurrence.size(), 2);
    QCOMPARE(timed.updated, QDateTime(QDate(2026, 10, 1), QTime(12, 0), QTimeZone::UTC));
    QVERIFY(!timed.start.isAllDay());

    const GoogleEvent &allDay = stored.at(1);
    QVERIFY(allDay.start.isAllDay());
    QCOMPARE(allDay.end.date, QDate(2026, 10, 13));
    QVERIFY(allDay.recurrence.isEmpty());

    const GoogleEvent &exception = stored.at(2);
    QCOMPARE(exception.recurringEventId, QStringLiteral("timed"));
    QCOMPARE(exception.originalStart.dateTime.toUTC(),
             QDateTime(QDate(2026, 10, 19), QTime(13, 30), QTimeZone::UTC));
    QCOMPARE(exception.start.dateTime.toUTC(),
             QDateTime(QDate(2026, 10, 19), QTime(15, 0), QTimeZone::UTC));
}

void TestGoogleCache::fullSyncReplacesEvents()
{
    apply({parsed(R"({"id":"a"})"), parsed(R"({"id":"b"})")}, true);
    apply({parsed(R"({"id":"c"})")}, true);

    QCOMPARE(ids(m_cache->events(kAccount, kCalendar)), QStringList{u"c"_s});
}

void TestGoogleCache::deletedEventIsRemoved()
{
    apply({parsed(R"({"id":"a"})"), parsed(R"({"id":"b"})")}, true);
    apply({parsed(R"({"id":"a","status":"cancelled"})"), parsed(R"({"id":"d"})")}, false);

    QCOMPARE(ids(m_cache->events(kAccount, kCalendar)), (QStringList{u"b"_s, u"d"_s}));
}

void TestGoogleCache::cancelledOccurrenceIsKept()
{
    apply({parsed(R"({"id":"s","recurrence":["RRULE:FREQ=DAILY"]})")}, true);
    apply({parsed(R"({"id":"s_1","status":"cancelled","recurringEventId":"s",
                    "originalStartTime":{"dateTime":"2026-10-06T10:00:00Z"}})")},
          false);

    const QList<GoogleEvent> stored = m_cache->events(kAccount, kCalendar);
    QCOMPARE(ids(stored), (QStringList{u"s"_s, u"s_1"_s}));
    QVERIFY(stored.at(1).isCancelled());
}

void TestGoogleCache::deletedSeriesTakesExceptions()
{
    apply({parsed(R"({"id":"s","recurrence":["RRULE:FREQ=DAILY"]})"),
           parsed(R"({"id":"s_1","recurringEventId":"s"})"), parsed(R"({"id":"other"})")},
          true);
    apply({parsed(R"({"id":"s","status":"cancelled"})")}, false);

    QCOMPARE(ids(m_cache->events(kAccount, kCalendar)), QStringList{u"other"_s});
}

void TestGoogleCache::syncTokenIsStoredWithChanges()
{
    apply({}, true, u"first"_s);
    QCOMPARE(m_cache->syncToken(kAccount, kCalendar), u"first"_s);
    apply({}, false, u"second"_s);
    QCOMPARE(m_cache->syncToken(kAccount, kCalendar), u"second"_s);
}

void TestGoogleCache::unknownCalendarIsRejected()
{
    QVERIFY(!m_cache->applyChanges(kAccount, u"nope"_s, {{parsed(R"({"id":"a"})")}, u"t"_s}, true));
    QVERIFY(!m_cache->errorString().isEmpty());
    // Rolled back, so the event did not land either.
    QVERIFY(m_cache->events(kAccount, u"nope"_s).isEmpty());
}

void TestGoogleCache::removedCalendarLosesEvents()
{
    QVERIFY(m_cache->setCalendars(kAccount, {calendar(kCalendar), calendar(u"team"_s)}));
    apply({parsed(R"({"id":"a"})")}, true);
    QVERIFY(m_cache->applyChanges(kAccount, u"team"_s, {{parsed(R"({"id":"t"})")}, u"t"_s}, true));

    QVERIFY(m_cache->setCalendars(kAccount, {calendar(u"team"_s)}));

    QCOMPARE(m_cache->calendars(kAccount).size(), 1);
    QVERIFY(m_cache->events(kAccount, kCalendar).isEmpty());
    QCOMPARE(m_cache->events(kAccount, u"team"_s).size(), 1);
}

void TestGoogleCache::keptCalendarKeepsToken()
{
    apply({parsed(R"({"id":"a"})")}, true, u"tok"_s);

    GoogleCalendar renamed = calendar(kCalendar);
    renamed.summary = u"Renamed"_s;
    QVERIFY(m_cache->setCalendars(kAccount, {renamed}));

    QCOMPARE(m_cache->syncToken(kAccount, kCalendar), u"tok"_s);
    QCOMPARE(m_cache->calendars(kAccount).first().summary, u"Renamed"_s);
    QCOMPARE(m_cache->events(kAccount, kCalendar).size(), 1);
}

void TestGoogleCache::accountsAreSeparate()
{
    QVERIFY(m_cache->setCalendars(kOther, {calendar(kCalendar)}));
    apply({parsed(R"({"id":"mine"})")}, true);
    QVERIFY(
        m_cache->applyChanges(kOther, kCalendar, {{parsed(R"({"id":"theirs"})")}, u"t"_s}, true));

    QCOMPARE(ids(m_cache->events(kAccount, kCalendar)), QStringList{u"mine"_s});

    QVERIFY(m_cache->removeAccount(kOther));
    QVERIFY(m_cache->calendars(kOther).isEmpty());
    QVERIFY(m_cache->events(kOther, kCalendar).isEmpty());
    QCOMPARE(m_cache->events(kAccount, kCalendar).size(), 1);
}

void TestGoogleCache::dataSurvivesReopen()
{
    apply({parsed(R"({"id":"a"})")}, true, u"tok"_s);
    m_cache = std::make_unique<GoogleCache>(path());
    QVERIFY(m_cache->open());

    QCOMPARE(m_cache->syncToken(kAccount, kCalendar), u"tok"_s);
    QCOMPARE(m_cache->events(kAccount, kCalendar).size(), 1);
}

void TestGoogleCache::outdatedSchemaIsRebuilt()
{
    apply({parsed(R"({"id":"a"})")}, true, u"tok"_s);
    m_cache.reset();
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, u"old"_s);
        db.setDatabaseName(path());
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec(u"PRAGMA user_version = 999"_s));
        db.close();
    }
    QSqlDatabase::removeDatabase(u"old"_s);

    m_cache = std::make_unique<GoogleCache>(path());
    QVERIFY2(m_cache->open(), qPrintable(m_cache->errorString()));

    // A dropped cache means a full sync, so the token must go with the data.
    QVERIFY(m_cache->calendars(kAccount).isEmpty());
    QCOMPARE(m_cache->syncToken(kAccount, kCalendar), QString());
}

QTEST_GUILESS_MAIN(TestGoogleCache)
#include "tst_googlecache.moc"
