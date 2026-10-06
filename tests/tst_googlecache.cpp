#include "callie/GoogleCache.h"

#include <QFileInfo>
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
    void rangeReadSkipsDistantEvents();
    void rangeReadKeepsEdgeCases();
    void syncTokenIsStoredWithChanges();
    void unknownCalendarIsRejected();
    void removedCalendarLosesEvents();
    void keptCalendarKeepsToken();
    void accountsAreSeparate();
    void dataSurvivesReopen();
    void outdatedSchemaIsRebuilt();
    void appliedChangesRecordSuccess();
    void calendarErrorKeepsLastSuccess();
    void accountSyncStateIsKept();
    void eventCountCoversAllCalendars();
    void readingDoesNotCreateACache();
    void readingSeesWhatWasWritten();
    void readingLeavesOtherSchemasAlone();

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

void TestGoogleCache::rangeReadSkipsDistantEvents()
{
    apply(
        {parsed(R"({"id":"inside","start":{"dateTime":"2026-10-06T10:00:00Z"},
                    "end":{"dateTime":"2026-10-06T11:00:00Z"}})"),
         parsed(R"({"id":"spanning","start":{"dateTime":"2026-10-01T10:00:00Z"},
                    "end":{"dateTime":"2026-10-20T11:00:00Z"}})"),
         parsed(R"({"id":"before","start":{"dateTime":"2026-10-04T10:00:00Z"},
                    "end":{"dateTime":"2026-10-04T11:00:00Z"}})"),
         parsed(R"({"id":"after","start":{"dateTime":"2026-10-12T10:00:00Z"},
                    "end":{"dateTime":"2026-10-12T11:00:00Z"}})"),
         parsed(R"({"id":"allday","start":{"date":"2026-10-11"},"end":{"date":"2026-10-12"}})"),
         parsed(R"({"id":"far-allday","start":{"date":"2026-11-11"},"end":{"date":"2026-11-12"}})"),
         parsed(R"({"id":"series","start":{"dateTime":"2020-01-06T10:00:00Z"},
                    "end":{"dateTime":"2020-01-06T11:00:00Z"},"recurrence":["RRULE:FREQ=WEEKLY"]})"),
         parsed(R"({"id":"moved-out","recurringEventId":"series",
                    "originalStartTime":{"dateTime":"2026-10-05T10:00:00Z"},
                    "start":{"dateTime":"2026-11-30T10:00:00Z"},
                    "end":{"dateTime":"2026-11-30T11:00:00Z"}})"),
         parsed(R"({"id":"moved-in","recurringEventId":"series",
                    "originalStartTime":{"dateTime":"2026-11-23T10:00:00Z"},
                    "start":{"dateTime":"2026-10-07T10:00:00Z"},
                    "end":{"dateTime":"2026-10-07T11:00:00Z"}})"),
         parsed(R"({"id":"cancelled","status":"cancelled","recurringEventId":"series",
                    "originalStartTime":{"dateTime":"2026-10-05T10:00:00Z"}})"),
         parsed(R"({"id":"far-exception","recurringEventId":"series",
                    "originalStartTime":{"dateTime":"2025-03-03T10:00:00Z"},
                    "start":{"dateTime":"2025-03-04T10:00:00Z"},
                    "end":{"dateTime":"2025-03-04T11:00:00Z"}})")},
        true);

    const QDateTime from(QDate(2026, 10, 5), QTime(0, 0), QTimeZone::UTC);
    QCOMPARE(ids(m_cache->events(kAccount, kCalendar, from, from.addDays(7))),
             (QStringList{u"allday"_s, u"cancelled"_s, u"inside"_s, u"moved-in"_s, u"moved-out"_s,
                          u"series"_s, u"spanning"_s}));
}

void TestGoogleCache::rangeReadKeepsEdgeCases()
{
    apply({parsed(R"({"id":"no-end","start":{"dateTime":"2026-10-06T10:00:00Z"}})"),
           parsed(R"({"id":"instant","start":{"dateTime":"2026-10-05T00:00:00Z"},
                    "end":{"dateTime":"2026-10-05T00:00:00Z"}})"),
           parsed(R"({"id":"allday-no-end","start":{"date":"2026-10-07"}})"),
           // Four-day trips every month; one starting three days before the
           // range is cancelled, though its days run into the range.
           parsed(R"({"id":"trip","start":{"date":"2026-01-02"},"end":{"date":"2026-01-06"},
                    "recurrence":["RRULE:FREQ=MONTHLY"]})"),
           parsed(R"({"id":"trip-cancelled","status":"cancelled","recurringEventId":"trip",
                    "originalStartTime":{"date":"2026-10-02"}})"),
           parsed(R"({"id":"trip-old","status":"cancelled","recurringEventId":"trip",
                    "originalStartTime":{"date":"2026-08-02"}})")},
          true);

    const QDateTime from(QDate(2026, 10, 5), QTime(0, 0), QTimeZone::UTC);
    QCOMPARE(ids(m_cache->events(kAccount, kCalendar, from, from.addDays(7))),
             (QStringList{u"allday-no-end"_s, u"instant"_s, u"no-end"_s, u"trip"_s,
                          u"trip-cancelled"_s}));
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

void TestGoogleCache::appliedChangesRecordSuccess()
{
    QVERIFY(m_cache->recordCalendarError(kAccount, kCalendar, u"Backend Error"_s));
    const QDateTime before = QDateTime::currentDateTimeUtc().addSecs(-1);
    apply({}, true);

    const SyncState state = m_cache->calendarState(kAccount, kCalendar);
    QVERIFY(state.lastSynced >= before);
    QCOMPARE(state.lastError, QString());
}

void TestGoogleCache::calendarErrorKeepsLastSuccess()
{
    apply({}, true);
    const QDateTime synced = m_cache->calendarState(kAccount, kCalendar).lastSynced;

    QVERIFY(m_cache->recordCalendarError(kAccount, kCalendar, u"Backend Error"_s));

    const SyncState state = m_cache->calendarState(kAccount, kCalendar);
    QCOMPARE(state.lastSynced, synced);
    QCOMPARE(state.lastError, u"Backend Error"_s);
}

void TestGoogleCache::accountSyncStateIsKept()
{
    QVERIFY(!m_cache->accountState(kAccount).lastSynced.isValid());

    QVERIFY(m_cache->recordAccountSync(kAccount, {}));
    const QDateTime synced = m_cache->accountState(kAccount).lastSynced;
    QVERIFY(synced.isValid());

    QVERIFY(m_cache->recordAccountSync(kAccount, u"keyring is locked"_s));
    QCOMPARE(m_cache->accountState(kAccount).lastSynced, synced);
    QCOMPARE(m_cache->accountState(kAccount).lastError, u"keyring is locked"_s);

    QVERIFY(m_cache->recordAccountSync(kAccount, {}));
    QCOMPARE(m_cache->accountState(kAccount).lastError, QString());

    QVERIFY(m_cache->removeAccount(kAccount));
    QVERIFY(!m_cache->accountState(kAccount).lastSynced.isValid());
}

void TestGoogleCache::eventCountCoversAllCalendars()
{
    QVERIFY(m_cache->setCalendars(kAccount, {calendar(kCalendar), calendar(u"team"_s)}));
    apply({parsed(R"({"id":"a"})"), parsed(R"({"id":"b"})")}, true);
    QVERIFY(m_cache->applyChanges(kAccount, u"team"_s, {{parsed(R"({"id":"t"})")}, u"t"_s}, true));

    QCOMPARE(m_cache->eventCount(kAccount), 3);
    QCOMPARE(m_cache->eventCount(kOther), 0);
}

void TestGoogleCache::readingSeesWhatWasWritten()
{
    apply({parsed(R"({"id":"a"})"), parsed(R"({"id":"b"})")}, true, u"tok"_s);
    QVERIFY(m_cache->recordAccountSync(kAccount, {}));

    // While the writer is still open, as when the app is running.
    GoogleCache reader(path());
    QVERIFY2(reader.openForReading(), qPrintable(reader.errorString()));

    QCOMPARE(reader.calendars(kAccount).size(), 1);
    QCOMPARE(reader.eventCount(kAccount), 2);
    QVERIFY(reader.accountState(kAccount).lastSynced.isValid());
    QVERIFY(!reader.recordAccountSync(kAccount, u"should not write"_s));
}

void TestGoogleCache::readingDoesNotCreateACache()
{
    const QString missing = m_dir->filePath(u"nowhere/google.sqlite"_s);
    GoogleCache cache(missing);

    QVERIFY(!cache.openForReading());
    QVERIFY(!QFileInfo::exists(missing));
}

void TestGoogleCache::readingLeavesOtherSchemasAlone()
{
    apply({parsed(R"({"id":"a"})")}, true, u"tok"_s);
    m_cache.reset();
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, u"old"_s);
        db.setDatabaseName(path());
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec(u"PRAGMA user_version = 99"_s));
        db.close();
    }
    QSqlDatabase::removeDatabase(u"old"_s);

    {
        GoogleCache reader(path());
        QVERIFY(!reader.openForReading());
        QVERIFY(reader.errorString().contains(u"99"_s));
    }

    // Still the same data, still schema 99: nothing was rebuilt.
    QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, u"check"_s);
    db.setDatabaseName(path());
    QVERIFY(db.open());
    {
        QSqlQuery query(db);
        QVERIFY(query.exec(u"PRAGMA user_version"_s) && query.next());
        QCOMPARE(query.value(0).toInt(), 99);
        QVERIFY(query.exec(u"SELECT COUNT(*) FROM events"_s) && query.next());
        QCOMPARE(query.value(0).toInt(), 1);
    }
    db.close();
    db = {};
    QSqlDatabase::removeDatabase(u"check"_s);
}

QTEST_GUILESS_MAIN(TestGoogleCache)
#include "tst_googlecache.moc"
