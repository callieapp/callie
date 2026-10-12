#include "FakeCalDavServer.h"
#include "FakeTokenStore.h"

#include "callie/CalDavSource.h"

#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestCalDavSource : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void firstSyncBringsCalendarsDown();
    void changesGoUpAndComeDown();
    void serversWithoutTokensCompareVersions();
    void theServersVersionWinsAConflict();
    void changesWaitWhileTheServerIsDown();
    void wrongPasswordsSaySo();

private:
    void sync();
    [[nodiscard]] QList<Event> week() const;
    [[nodiscard]] QString calendarId(const QString &name) const;

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<FakeCalDavServer> m_server;
    std::unique_ptr<QNetworkAccessManager> m_network;
    std::unique_ptr<FakeTokenStore> m_tokens;
    std::unique_ptr<CalDavSource> m_source;
};

namespace {

const QDateTime kMonday(QDate(2026, 10, 5), QTime(0, 0), QTimeZone::UTC);

QByteArray calendarText(const QByteArray &uid, const QByteArray &summary, int hour = 12)
{
    const QByteArray start = QByteArray::number(hour).rightJustified(2, '0');
    const QByteArray end = QByteArray::number(hour + 1).rightJustified(2, '0');
    return "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//t//EN\r\nBEGIN:VEVENT\r\nUID:" + uid +
           "\r\nDTSTART:20261006T" + start + "0000Z\r\nDTEND:20261006T" + end +
           "0000Z\r\nSUMMARY:" + summary + "\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
}

QString outcome(const std::function<void(CalendarSource::Created)> &call)
{
    QString error = u"no answer"_s;
    call([&error](const Outcome &result) { error = result.error; });
    return error;
}

} // namespace

void TestCalDavSource::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_server = std::make_unique<FakeCalDavServer>();
    m_server->addCalendar("work", "Work");
    m_server->addCalendar("shared", "Team", false);
    m_server->putElsewhere("work", "lunch.ics", calendarText("lunch", "Lunch"));
    m_network = std::make_unique<QNetworkAccessManager>();
    m_tokens = std::make_unique<FakeTokenStore>();
    m_tokens->secrets.insert(u"me@example.com"_s, u"app-password"_s);
    m_source = std::make_unique<CalDavSource>(m_dir->path(), m_network.get(), m_tokens.get());
    m_source->setAccounts({{u"me@example.com"_s, m_server->url(), u"me"_s}});
}

void TestCalDavSource::sync()
{
    QSignalSpy synced(m_source.get(), &CalDavSource::synced);
    m_source->refresh();
    QVERIFY(synced.wait(5000));
}

QList<Event> TestCalDavSource::week() const
{
    return m_source->eventsBetween(kMonday, kMonday.addDays(7), QTimeZone::UTC);
}

QString TestCalDavSource::calendarId(const QString &name) const
{
    for (const CalendarInfo &calendar : m_source->calendars()) {
        if (calendar.displayName == name)
            return calendar.id;
    }
    return {};
}

void TestCalDavSource::firstSyncBringsCalendarsDown()
{
    sync();
    QVERIFY2(m_source->lastError().isEmpty(), qPrintable(m_source->lastError()));
    const QList<CalendarInfo> calendars = m_source->calendars();
    QCOMPARE(calendars.size(), 2);
    QVERIFY(!calendarId(u"Work"_s).isEmpty());
    for (const CalendarInfo &calendar : calendars) {
        QCOMPARE(calendar.account, u"me@example.com"_s);
        QCOMPARE(calendar.writable, calendar.displayName == u"Work");
    }
    QCOMPARE(week().size(), 1);
    QCOMPARE(week().first().summary, u"Lunch"_s);
    QVERIFY(m_source->lastSynced().isValid());
}

void TestCalDavSource::changesGoUpAndComeDown()
{
    sync();
    const QString work = calendarId(u"Work"_s);
    QSignalSpy synced(m_source.get(), &CalDavSource::synced);

    // Made here, it lands on the server.
    EventDraft draft;
    draft.calendarId = work;
    draft.summary = u"Planning"_s;
    draft.start = kMonday.addDays(2).addSecs(9 * 3600);
    draft.end = draft.start.addSecs(3600);
    draft.id = u"planning"_s;
    QVERIFY(outcome([&](auto done) { m_source->createEvent(draft, done); }).isEmpty());
    QVERIFY(!m_source->waitingChanges().isEmpty());
    QVERIFY(synced.wait(5000));
    QVERIFY(m_source->waitingChanges().isEmpty());
    const auto &items = m_server->calendars[FakeCalDavServer::calendarPath("work")].items;
    QVERIFY(items.contains("/dav/calendars/me/work/planning.ics"));

    // Moved here, the server's copy moves; deleted here, it goes.
    Event lunch;
    for (const Event &each : week()) {
        if (each.summary == u"Lunch")
            lunch = each;
    }
    QVERIFY(outcome([&](auto done) {
                m_source->moveEvent(lunch, lunch.start.addSecs(3600), lunch.end.addSecs(3600),
                                    false, done);
            }).isEmpty());
    QVERIFY(synced.wait(5000));
    QVERIFY(items.value("/dav/calendars/me/work/lunch.ics").data.contains("T130000Z"));
    QVERIFY(outcome([&](auto done) { m_source->deleteEvent(lunch, false, done); }).isEmpty());
    QVERIFY(synced.wait(5000));
    QVERIFY(!items.contains("/dav/calendars/me/work/lunch.ics"));

    // Made elsewhere, it comes down; deleted elsewhere, it goes here too.
    m_server->putElsewhere("work", "dentist.ics", calendarText("dentist", "Dentist", 15));
    m_server->removeElsewhere("work", "planning.ics");
    sync();
    QCOMPARE(week().size(), 1);
    QCOMPARE(week().first().summary, u"Dentist"_s);

    // A calendar the user may only read takes nothing.
    draft.calendarId = calendarId(u"Team"_s);
    QVERIFY(!outcome([&](auto done) { m_source->createEvent(draft, done); }).isEmpty());
}

void TestCalDavSource::serversWithoutTokensCompareVersions()
{
    m_server->syncTokens = false;
    sync();
    QCOMPARE(week().size(), 1);
    m_server->putElsewhere("work", "lunch.ics", calendarText("lunch", "Lunch, moved", 14));
    m_server->putElsewhere("work", "dentist.ics", calendarText("dentist", "Dentist", 15));
    sync();
    QStringList summaries;
    for (const Event &each : week())
        summaries << each.summary;
    summaries.sort();
    QCOMPARE(summaries, (QStringList{u"Dentist"_s, u"Lunch, moved"_s}));
    m_server->removeElsewhere("work", "dentist.ics");
    sync();
    QCOMPARE(week().size(), 1);
}

void TestCalDavSource::theServersVersionWinsAConflict()
{
    sync();
    QSignalSpy errors(m_source.get(), &CalendarSource::errorOccurred);
    QSignalSpy synced(m_source.get(), &CalDavSource::synced);
    // Changed elsewhere after the last sync, and here before the next.
    m_server->putElsewhere("work", "lunch.ics", calendarText("lunch", "Lunch with Sam"));
    const Event lunch = week().first();
    QVERIFY(outcome([&](auto done) {
                m_source->moveEvent(lunch, lunch.start.addSecs(3600), lunch.end.addSecs(3600),
                                    false, done);
            }).isEmpty());
    QVERIFY(synced.wait(5000));
    QCOMPARE(errors.size(), 1);
    QCOMPARE(week().first().summary, u"Lunch with Sam"_s);
    QCOMPARE(week().first().start.time(), QTime(12, 0));
    QVERIFY(m_source->waitingChanges().isEmpty());
}

void TestCalDavSource::changesWaitWhileTheServerIsDown()
{
    sync();
    QSignalSpy synced(m_source.get(), &CalDavSource::synced);
    const auto up = std::move(m_server->server.replier);
    m_server->server.replier = [](const FakeHttpServer::Request &) {
        return FakeHttpServer::Reply{503, "down", {}, "text/plain"};
    };
    const Event lunch = week().first();
    QVERIFY(outcome([&](auto done) {
                m_source->moveEvent(lunch, lunch.start.addSecs(3600), lunch.end.addSecs(3600),
                                    false, done);
            }).isEmpty());
    QVERIFY(synced.wait(5000));
    // Kept, as made, until the server is back.
    QCOMPARE(week().first().start.time(), QTime(13, 0));
    QVERIFY(!m_source->waitingChanges().isEmpty());
    QVERIFY(!m_source->lastError().isEmpty());

    m_server->server.replier = up;
    sync();
    QVERIFY(m_source->waitingChanges().isEmpty());
    QVERIFY(m_source->lastError().isEmpty());
    QVERIFY(m_server->calendars[FakeCalDavServer::calendarPath("work")]
                .items.value("/dav/calendars/me/work/lunch.ics")
                .data.contains("T130000Z"));
}

void TestCalDavSource::wrongPasswordsSaySo()
{
    m_tokens->secrets.insert(u"me@example.com"_s, u"guess"_s);
    sync();
    QVERIFY(m_source->lastError().contains(u"app password"_s));
    QVERIFY(m_source->calendars().isEmpty());
    m_tokens->secrets.clear();
    sync();
    QVERIFY(m_source->lastError().contains(u"sign in again"_s));
}

QTEST_MAIN(TestCalDavSource)
#include "tst_caldavsource.moc"
