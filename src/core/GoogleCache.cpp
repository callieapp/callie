#include "callie/GoogleCache.h"

#include "callie/Logging.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTimeZone>
#include <QUuid>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

// Bump when the tables change. Older caches are dropped and fully re-synced.
constexpr int kSchemaVersion = 2;

const char *const kSchema[] = {
    R"(CREATE TABLE calendars (
        account TEXT NOT NULL,
        id TEXT NOT NULL,
        summary TEXT, time_zone TEXT, color TEXT, access_role TEXT,
        is_primary INTEGER NOT NULL DEFAULT 0,
        selected INTEGER NOT NULL DEFAULT 0,
        sync_token TEXT,
        last_synced TEXT, last_error TEXT,
        PRIMARY KEY (account, id)))",
    R"(CREATE TABLE accounts (
        account TEXT PRIMARY KEY,
        last_synced TEXT, last_error TEXT))",
    R"(CREATE TABLE events (
        account TEXT NOT NULL,
        calendar_id TEXT NOT NULL,
        id TEXT NOT NULL,
        status TEXT, summary TEXT, description TEXT, location TEXT, conference_url TEXT,
        start_date TEXT, start_time TEXT, start_zone TEXT,
        end_date TEXT, end_time TEXT, end_zone TEXT,
        recurrence TEXT,
        recurring_event_id TEXT,
        original_date TEXT, original_time TEXT, original_zone TEXT,
        updated TEXT,
        PRIMARY KEY (account, calendar_id, id)))",
    "CREATE INDEX events_series ON events (account, calendar_id, recurring_event_id)",
};

QString accountKey(const Account &account)
{
    return account.provider + u'/' + account.id;
}

// Instants are stored in UTC; the zone name is kept beside them so a reload
// gets back the same wall-clock time the organizer chose.
void bindTime(QSqlQuery &query, const QString &prefix, const GoogleEventTime &time)
{
    query.bindValue(u":"_s + prefix + u"_date"_s,
                    time.date.isValid() ? time.date.toString(Qt::ISODate) : QVariant());
    query.bindValue(u":"_s + prefix + u"_time"_s,
                    time.dateTime.isValid() ? time.dateTime.toUTC().toString(Qt::ISODateWithMs)
                                            : QVariant());
    query.bindValue(u":"_s + prefix + u"_zone"_s, time.timeZone);
}

GoogleEventTime readTime(const QSqlQuery &query, const QString &prefix)
{
    GoogleEventTime time;
    time.date = QDate::fromString(query.value(prefix + u"_date"_s).toString(), Qt::ISODate);
    time.timeZone = query.value(prefix + u"_zone"_s).toString();
    time.dateTime =
        QDateTime::fromString(query.value(prefix + u"_time"_s).toString(), Qt::ISODateWithMs);
    const QTimeZone zone(time.timeZone.toUtf8());
    if (time.dateTime.isValid() && zone.isValid())
        time.dateTime = time.dateTime.toTimeZone(zone);
    return time;
}

} // namespace

GoogleCache::GoogleCache(const QString &path)
    : m_path(path), m_connection(QUuid::createUuid().toString(QUuid::WithoutBraces))
{}

GoogleCache::~GoogleCache()
{
    if (!QSqlDatabase::contains(m_connection))
        return;
    QSqlDatabase::database(m_connection, false).close();
    QSqlDatabase::removeDatabase(m_connection);
}

QString GoogleCache::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
           QStringLiteral("/callie/google.sqlite");
}

bool GoogleCache::open()
{
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath()))
        return fail(QObject::tr("could not create the directory for %1").arg(m_path));

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    db.setDatabaseName(m_path);
    // The app and the CLI can sync at the same time.
    db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    if (!db.open())
        return fail(db.lastError().text());
    m_open = true;

    QSqlQuery version(db);
    if (!version.exec(QStringLiteral("PRAGMA user_version")) || !version.next())
        return fail(version.lastError().text());
    const int found = version.value(0).toInt();
    version.finish();
    if (found == kSchemaVersion)
        return exec(QStringLiteral("PRAGMA journal_mode=WAL"));

    if (found != 0)
        qCInfo(lcSync) << "cache schema" << found << "is outdated, starting over";
    if (!db.transaction())
        return fail(db.lastError().text());
    bool ok = exec(QStringLiteral("DROP TABLE IF EXISTS accounts")) &&
              exec(QStringLiteral("DROP TABLE IF EXISTS events")) &&
              exec(QStringLiteral("DROP TABLE IF EXISTS calendars"));
    for (const char *statement : kSchema)
        ok = ok && exec(QString::fromLatin1(statement));
    ok = ok && exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion));
    if (!ok || !db.commit()) {
        db.rollback();
        return ok ? fail(db.lastError().text()) : false;
    }
    return exec(QStringLiteral("PRAGMA journal_mode=WAL"));
}

bool GoogleCache::openForReading()
{
    if (!QFileInfo::exists(m_path))
        return fail(QObject::tr("no cache yet"));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    db.setDatabaseName(m_path);
    db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000"));
    if (!db.open())
        return fail(db.lastError().text());

    QSqlQuery version(db);
    if (!version.exec(QStringLiteral("PRAGMA user_version")) || !version.next())
        return fail(version.lastError().text());
    const int found = version.value(0).toInt();
    if (found != kSchemaVersion)
        return fail(
            QObject::tr("cache schema %1, this build reads %2").arg(found).arg(kSchemaVersion));
    m_open = true;
    return true;
}

QList<GoogleCalendar> GoogleCache::calendars(const Account &account)
{
    QList<GoogleCalendar> result;
    if (!m_open)
        return result;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("SELECT id, summary, time_zone, color, access_role, is_primary, "
                                 "selected FROM calendars WHERE account = ? ORDER BY rowid"));
    query.addBindValue(accountKey(account));
    if (!query.exec()) {
        fail(query.lastError().text());
        return result;
    }
    while (query.next()) {
        result.append(GoogleCalendar{
            .id = query.value(0).toString(),
            .summary = query.value(1).toString(),
            .timeZone = query.value(2).toString(),
            .color = query.value(3).toString(),
            .accessRole = query.value(4).toString(),
            .primary = query.value(5).toBool(),
            .selected = query.value(6).toBool(),
        });
    }
    return result;
}

QString GoogleCache::syncToken(const Account &account, const QString &calendarId)
{
    if (!m_open)
        return {};
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("SELECT sync_token FROM calendars WHERE account = ? AND id = ?"));
    query.addBindValue(accountKey(account));
    query.addBindValue(calendarId);
    if (!query.exec() || !query.next())
        return {};
    return query.value(0).toString();
}

bool GoogleCache::setCalendars(const Account &account, const QList<GoogleCalendar> &calendars)
{
    if (!m_open)
        return fail(QObject::tr("the cache is not open"));
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    if (!db.transaction())
        return fail(db.lastError().text());

    const QString key = accountKey(account);
    QStringList keep;
    bool ok = true;
    QSqlQuery upsert(db);
    upsert.prepare(QStringLiteral(
        "INSERT INTO calendars (account, id, summary, time_zone, color, access_role, is_primary, "
        "selected) VALUES (:account, :id, :summary, :zone, :color, :role, :primary, :selected) "
        "ON CONFLICT (account, id) DO UPDATE SET summary = excluded.summary, "
        "time_zone = excluded.time_zone, color = excluded.color, "
        "access_role = excluded.access_role, is_primary = excluded.is_primary, "
        "selected = excluded.selected"));
    for (const GoogleCalendar &calendar : calendars) {
        upsert.bindValue(QStringLiteral(":account"), key);
        upsert.bindValue(QStringLiteral(":id"), calendar.id);
        upsert.bindValue(QStringLiteral(":summary"), calendar.summary);
        upsert.bindValue(QStringLiteral(":zone"), calendar.timeZone);
        upsert.bindValue(QStringLiteral(":color"), calendar.color);
        upsert.bindValue(QStringLiteral(":role"), calendar.accessRole);
        upsert.bindValue(QStringLiteral(":primary"), calendar.primary);
        upsert.bindValue(QStringLiteral(":selected"), calendar.selected);
        if (!upsert.exec()) {
            ok = fail(upsert.lastError().text());
            break;
        }
        keep.append(calendar.id);
    }

    if (ok) {
        QSqlQuery stored(db);
        stored.prepare(QStringLiteral("SELECT id FROM calendars WHERE account = ?"));
        stored.addBindValue(key);
        QStringList gone;
        if (stored.exec()) {
            while (stored.next()) {
                if (!keep.contains(stored.value(0).toString()))
                    gone.append(stored.value(0).toString());
            }
        } else {
            ok = fail(stored.lastError().text());
        }
        for (const QString &id : std::as_const(gone)) {
            for (const QString &table : {QStringLiteral("events"), QStringLiteral("calendars")}) {
                QSqlQuery remove(db);
                remove.prepare(QStringLiteral("DELETE FROM %1 WHERE account = ? AND %2 = ?")
                                   .arg(table, table == u"events" ? u"calendar_id" : u"id"));
                remove.addBindValue(key);
                remove.addBindValue(id);
                if (ok && !remove.exec())
                    ok = fail(remove.lastError().text());
            }
        }
    }

    if (!ok || !db.commit()) {
        db.rollback();
        return ok ? fail(db.lastError().text()) : false;
    }
    return true;
}

bool GoogleCache::applyChanges(const Account &account, const QString &calendarId,
                               const GoogleEventChanges &changes, bool full)
{
    if (!m_open)
        return fail(QObject::tr("the cache is not open"));
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    if (!db.transaction())
        return fail(db.lastError().text());

    const QString key = accountKey(account);
    const auto run = [this](QSqlQuery &query) {
        return query.exec() || fail(query.lastError().text());
    };

    bool ok = true;
    if (full) {
        QSqlQuery clear(db);
        clear.prepare(QStringLiteral("DELETE FROM events WHERE account = ? AND calendar_id = ?"));
        clear.addBindValue(key);
        clear.addBindValue(calendarId);
        ok = run(clear);
    }

    QSqlQuery upsert(db);
    upsert.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO events (account, calendar_id, id, status, summary, description, "
        "location, conference_url, start_date, start_time, start_zone, end_date, end_time, "
        "end_zone, recurrence, recurring_event_id, original_date, original_time, original_zone, "
        "updated) VALUES (:account, :calendar, :id, :status, :summary, :description, :location, "
        ":conference, :start_date, :start_time, :start_zone, :end_date, :end_time, :end_zone, "
        ":recurrence, :series, :original_date, :original_time, :original_zone, :updated)"));
    QSqlQuery remove(db);
    remove.prepare(QStringLiteral("DELETE FROM events WHERE account = :account AND "
                                  "calendar_id = :calendar AND (id = :id OR "
                                  "recurring_event_id = :id)"));

    for (const GoogleEvent &event : changes.events) {
        if (!ok)
            break;
        // A cancelled event that belongs to a series is a deleted occurrence,
        // which recurrence expansion needs. Any other cancellation is a
        // deletion, and a deleted series takes its exceptions with it.
        if (event.isCancelled() && event.recurringEventId.isEmpty()) {
            remove.bindValue(QStringLiteral(":account"), key);
            remove.bindValue(QStringLiteral(":calendar"), calendarId);
            remove.bindValue(QStringLiteral(":id"), event.id);
            ok = run(remove);
            continue;
        }
        upsert.bindValue(QStringLiteral(":account"), key);
        upsert.bindValue(QStringLiteral(":calendar"), calendarId);
        upsert.bindValue(QStringLiteral(":id"), event.id);
        upsert.bindValue(QStringLiteral(":status"), event.status);
        upsert.bindValue(QStringLiteral(":summary"), event.summary);
        upsert.bindValue(QStringLiteral(":description"), event.description);
        upsert.bindValue(QStringLiteral(":location"), event.location);
        upsert.bindValue(QStringLiteral(":conference"), event.conferenceUrl.toString());
        bindTime(upsert, QStringLiteral("start"), event.start);
        bindTime(upsert, QStringLiteral("end"), event.end);
        upsert.bindValue(QStringLiteral(":recurrence"), event.recurrence.join(u'\n'));
        upsert.bindValue(QStringLiteral(":series"), event.recurringEventId);
        bindTime(upsert, QStringLiteral("original"), event.originalStart);
        upsert.bindValue(QStringLiteral(":updated"), event.updated.toString(Qt::ISODateWithMs));
        ok = run(upsert);
    }

    if (ok) {
        QSqlQuery token(db);
        token.prepare(QStringLiteral("UPDATE calendars SET sync_token = ?, last_synced = ?, "
                                     "last_error = NULL WHERE account = ? AND id = ?"));
        token.addBindValue(changes.nextSyncToken);
        token.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        token.addBindValue(key);
        token.addBindValue(calendarId);
        ok = run(token);
        if (ok && token.numRowsAffected() != 1)
            ok = fail(QObject::tr("unknown calendar %1").arg(calendarId));
    }

    if (!ok || !db.commit()) {
        db.rollback();
        return ok ? fail(db.lastError().text()) : false;
    }
    return true;
}

QList<GoogleEvent> GoogleCache::events(const Account &account, const QString &calendarId)
{
    if (!m_open)
        return {};
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("SELECT * FROM events WHERE account = ? AND calendar_id = ? "
                                 "ORDER BY rowid"));
    query.addBindValue(accountKey(account));
    query.addBindValue(calendarId);
    return readEvents(query);
}

QList<GoogleEvent> GoogleCache::events(const Account &account, const QString &calendarId,
                                       const QDateTime &from, const QDateTime &to)
{
    if (!m_open)
        return {};
    QSqlDatabase db = QSqlDatabase::database(m_connection);

    // Every series can reach the range, so they are read whole.
    QSqlQuery series(db);
    series.prepare(QStringLiteral("SELECT * FROM events WHERE account = ? AND calendar_id = ? "
                                  "AND IFNULL(recurrence, '') <> '' ORDER BY rowid"));
    series.addBindValue(accountKey(account));
    series.addBindValue(calendarId);
    QList<GoogleEvent> result = readEvents(series);

    // An exception matters while the occurrence it replaces could overlap the
    // range, which reaches back by the longest occurrence.
    qint64 longest = 0;
    for (const GoogleEvent &event : std::as_const(result)) {
        const qint64 span = event.start.isAllDay()
                                ? event.start.date.daysTo(event.end.date) * 86400
                                : event.start.dateTime.secsTo(event.end.dateTime);
        longest = std::max(longest, span);
    }

    // Instants are stored as UTC ISO strings, so they compare as text.
    // Floating all-day dates get a day of slack for any viewing zone.
    const QDateTime start = from.toUTC();
    const QDateTime end = to.toUTC();
    QSqlQuery rest(db);
    rest.prepare(QStringLiteral(
        "SELECT * FROM events WHERE account = :account AND calendar_id = :calendar AND "
        "IFNULL(recurrence, '') = '' AND ("
        "(start_time < :end AND IFNULL(end_time, start_time) >= :start) "
        "OR (start_date < :end_date AND IFNULL(end_date, start_date) >= :start_date) "
        "OR (original_time >= :original_start AND original_time < :end) "
        "OR (original_date >= :original_start_date AND original_date < :end_date)) "
        "ORDER BY rowid"));
    rest.bindValue(QStringLiteral(":account"), accountKey(account));
    rest.bindValue(QStringLiteral(":calendar"), calendarId);
    rest.bindValue(QStringLiteral(":start"), start.toString(Qt::ISODateWithMs));
    rest.bindValue(QStringLiteral(":end"), end.toString(Qt::ISODateWithMs));
    rest.bindValue(QStringLiteral(":start_date"), from.date().addDays(-1).toString(Qt::ISODate));
    rest.bindValue(QStringLiteral(":end_date"), to.date().addDays(1).toString(Qt::ISODate));
    rest.bindValue(QStringLiteral(":original_start"),
                   start.addSecs(-longest).toString(Qt::ISODateWithMs));
    rest.bindValue(QStringLiteral(":original_start_date"),
                   from.date().addDays(-1 - (longest + 86399) / 86400).toString(Qt::ISODate));
    result.append(readEvents(rest));
    return result;
}

QList<GoogleEvent> GoogleCache::readEvents(QSqlQuery &query)
{
    QList<GoogleEvent> result;
    if (!query.exec()) {
        fail(query.lastError().text());
        return result;
    }
    while (query.next()) {
        GoogleEvent event;
        event.id = query.value(u"id"_s).toString();
        event.status = query.value(u"status"_s).toString();
        event.summary = query.value(u"summary"_s).toString();
        event.description = query.value(u"description"_s).toString();
        event.location = query.value(u"location"_s).toString();
        event.conferenceUrl = QUrl(query.value(u"conference_url"_s).toString());
        event.start = readTime(query, QStringLiteral("start"));
        event.end = readTime(query, QStringLiteral("end"));
        const QString recurrence = query.value(u"recurrence"_s).toString();
        if (!recurrence.isEmpty())
            event.recurrence = recurrence.split(u'\n');
        event.recurringEventId = query.value(u"recurring_event_id"_s).toString();
        event.originalStart = readTime(query, QStringLiteral("original"));
        event.updated =
            QDateTime::fromString(query.value(u"updated"_s).toString(), Qt::ISODateWithMs);
        result.append(event);
    }
    return result;
}

bool GoogleCache::recordCalendarError(const Account &account, const QString &calendarId,
                                      const QString &error)
{
    if (!m_open)
        return fail(QObject::tr("the cache is not open"));
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(
        QStringLiteral("UPDATE calendars SET last_error = ? WHERE account = ? AND id = ?"));
    query.addBindValue(error);
    query.addBindValue(accountKey(account));
    query.addBindValue(calendarId);
    return query.exec() || fail(query.lastError().text());
}

SyncState GoogleCache::calendarState(const Account &account, const QString &calendarId)
{
    if (!m_open)
        return {};
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral(
        "SELECT last_synced, last_error FROM calendars WHERE account = ? AND id = ?"));
    query.addBindValue(accountKey(account));
    query.addBindValue(calendarId);
    if (!query.exec() || !query.next())
        return {};
    return {QDateTime::fromString(query.value(0).toString(), Qt::ISODateWithMs),
            query.value(1).toString()};
}

bool GoogleCache::recordAccountSync(const Account &account, const QString &error)
{
    if (!m_open)
        return fail(QObject::tr("the cache is not open"));
    QSqlQuery query(QSqlDatabase::database(m_connection));
    // A failure keeps the time of the last success.
    query.prepare(error.isEmpty()
                      ? QStringLiteral("INSERT INTO accounts (account, last_synced, last_error) "
                                       "VALUES (:account, :now, NULL) ON CONFLICT (account) DO "
                                       "UPDATE SET last_synced = :now, last_error = NULL")
                      : QStringLiteral("INSERT INTO accounts (account, last_error) VALUES "
                                       "(:account, :error) ON CONFLICT (account) DO UPDATE SET "
                                       "last_error = :error"));
    query.bindValue(QStringLiteral(":account"), accountKey(account));
    if (error.isEmpty())
        query.bindValue(QStringLiteral(":now"),
                        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    else
        query.bindValue(QStringLiteral(":error"), error);
    return query.exec() || fail(query.lastError().text());
}

SyncState GoogleCache::accountState(const Account &account)
{
    if (!m_open)
        return {};
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("SELECT last_synced, last_error FROM accounts WHERE account = ?"));
    query.addBindValue(accountKey(account));
    if (!query.exec() || !query.next())
        return {};
    return {QDateTime::fromString(query.value(0).toString(), Qt::ISODateWithMs),
            query.value(1).toString()};
}

int GoogleCache::eventCount(const Account &account)
{
    if (!m_open)
        return 0;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM events WHERE account = ?"));
    query.addBindValue(accountKey(account));
    return query.exec() && query.next() ? query.value(0).toInt() : 0;
}

bool GoogleCache::removeAccount(const Account &account)
{
    if (!m_open)
        return fail(QObject::tr("the cache is not open"));
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    if (!db.transaction())
        return fail(db.lastError().text());
    bool ok = true;
    for (const QString &table :
         {QStringLiteral("events"), QStringLiteral("calendars"), QStringLiteral("accounts")}) {
        QSqlQuery remove(db);
        remove.prepare(QStringLiteral("DELETE FROM %1 WHERE account = ?").arg(table));
        remove.addBindValue(accountKey(account));
        if (!remove.exec()) {
            ok = fail(remove.lastError().text());
            break;
        }
    }
    if (!ok || !db.commit()) {
        db.rollback();
        return ok ? fail(db.lastError().text()) : false;
    }
    return true;
}

bool GoogleCache::exec(const QString &statement)
{
    QSqlQuery query(QSqlDatabase::database(m_connection));
    return query.exec(statement) || fail(query.lastError().text());
}

bool GoogleCache::fail(const QString &message)
{
    m_error = message;
    // Info, not warning: callers report the failure to the user.
    qCInfo(lcSync) << "cache error:" << message;
    return false;
}

} // namespace callie
