#include "FakeCalDavServer.h"
#include "FakeTokenStore.h"

#include "callie/CalDavSource.h"
#include "callie/Settings.h"

#include <QDir>
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
    void aForgottenTokenStartsOver();
    void busyOrSignedOutServersKeepTheChange();
    void aChangeMadeWhileOneGoesUpGoesToo();
    void removingAnAccountStopsItsSync();
    void aCalendarGoneKeepsChangesNotSent();
    void anotherServerStartsAfresh();
    void theSameServerWrittenOtherwiseKeepsItsCopies();
    void resyncStartsOver();
    void guestsAddedAreForTheServerToInvite();
    void guestsAlreadyThereAreLeftAlone();
    void noAddressNoInvitations();
    void aNewLookGoesToTheServer();
    void aLookTurnedDownComesBack();
    void looksGivenInSettingsBecomeTheCalendars();

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

void TestCalDavSource::aForgottenTokenStartsOver()
{
    sync();
    QCOMPARE(week().size(), 1);
    // Deleted elsewhere while the server forgot where Callie was.
    m_server->removeElsewhere("work", "lunch.ics");
    m_server->forgetTokens = true;
    sync();
    QVERIFY2(m_source->lastError().isEmpty(), qPrintable(m_source->lastError()));
    QVERIFY(week().isEmpty());
}

void TestCalDavSource::busyOrSignedOutServersKeepTheChange()
{
    sync();
    QSignalSpy synced(m_source.get(), &CalDavSource::synced);
    const auto up = m_server->server.replier;
    for (const int status : {429, 401}) {
        m_server->server.replier = [up, status](const FakeHttpServer::Request &request) {
            if (request.method == "PUT")
                return FakeHttpServer::Reply{status, "", {}, "text/plain"};
            return up(request);
        };
        const Event lunch = week().first();
        QVERIFY(outcome([&](auto done) {
                    m_source->moveEvent(lunch, lunch.start.addSecs(3600), lunch.end.addSecs(3600),
                                        false, done);
                }).isEmpty());
        QVERIFY(synced.wait(5000));
        // Kept as made, and still to go.
        QCOMPARE(week().first().start, lunch.start.addSecs(3600));
        QVERIFY(!m_source->waitingChanges().isEmpty());
    }
    m_server->server.replier = up;
    sync();
    QVERIFY(m_source->waitingChanges().isEmpty());
    QVERIFY(m_server->calendars[FakeCalDavServer::calendarPath("work")]
                .items.value("/dav/calendars/me/work/lunch.ics")
                .data.contains("T140000Z"));
}

void TestCalDavSource::aChangeMadeWhileOneGoesUpGoesToo()
{
    sync();
    QSignalSpy synced(m_source.get(), &CalDavSource::synced);
    const auto up = m_server->server.replier;
    bool again = true;
    // While the first move is on its way, the event is moved once more.
    m_server->server.replier = [this, up, &again](const FakeHttpServer::Request &request) {
        if (request.method == "PUT" && std::exchange(again, false)) {
            const Event lunch = week().first();
            m_source->moveEvent(lunch, lunch.start.addSecs(3600), lunch.end.addSecs(3600), false,
                                [](const Outcome &) {});
        }
        return up(request);
    };
    const Event lunch = week().first();
    QVERIFY(outcome([&](auto done) {
                m_source->moveEvent(lunch, lunch.start.addSecs(3600), lunch.end.addSecs(3600),
                                    false, done);
            }).isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!again && m_source->waitingChanges().isEmpty() && !m_source->syncing(),
                             5000);
    QCOMPARE(week().first().start.time(), QTime(14, 0));
    QVERIFY(m_server->calendars[FakeCalDavServer::calendarPath("work")]
                .items.value("/dav/calendars/me/work/lunch.ics")
                .data.contains("T140000Z"));
}

void TestCalDavSource::removingAnAccountStopsItsSync()
{
    sync();
    QSignalSpy synced(m_source.get(), &CalDavSource::synced);
    m_source->refresh();
    QVERIFY(m_source->syncing());
    QString error;
    QVERIFY(m_source->forget(u"me@example.com"_s, &error));
    QVERIFY(!m_source->syncing());
    QCOMPARE(synced.size(), 1);
    QTest::qWait(300);
    QVERIFY(m_source->calendars().isEmpty());
    QVERIFY(QDir(m_dir->path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
}

void TestCalDavSource::aCalendarGoneKeepsChangesNotSent()
{
    sync();
    QSignalSpy synced(m_source.get(), &CalDavSource::synced);
    const auto up = m_server->server.replier;
    // A change that cannot go yet, then the calendar is deleted on the server.
    m_server->server.replier = [up](const FakeHttpServer::Request &request) {
        if (request.method == "PUT")
            return FakeHttpServer::Reply{503, "", {}, "text/plain"};
        return up(request);
    };
    const Event lunch = week().first();
    QVERIFY(outcome([&](auto done) {
                m_source->moveEvent(lunch, lunch.start.addSecs(3600), lunch.end.addSecs(3600),
                                    false, done);
            }).isEmpty());
    QVERIFY(synced.wait(5000));
    m_server->server.replier = up;
    m_server->calendars.remove(FakeCalDavServer::calendarPath("work"));
    qsizetype puts = 0;
    for (const auto &request : m_server->server.requests)
        puts += request.method == "PUT" ? 1 : 0;
    sync();

    // Kept as made, read-only, never sent anywhere, and the user is told.
    QCOMPARE(week().first().start.time(), QTime(13, 0));
    QVERIFY(!m_source->calendars().isEmpty());
    for (const CalendarInfo &calendar : m_source->calendars())
        QVERIFY(!calendar.writable);
    qsizetype after = 0;
    for (const auto &request : m_server->server.requests)
        after += request.method == "PUT" ? 1 : 0;
    QCOMPARE(after, puts);
    QVERIFY2(m_source->lastError().contains(u"gone from the server"_s),
             qPrintable(m_source->lastError()));
}

void TestCalDavSource::anotherServerStartsAfresh()
{
    sync();
    QCOMPARE(m_source->calendars().size(), 2);
    // Signed in again to another server: its calendars, and none of the old.
    FakeCalDavServer other;
    other.addCalendar("home", "Home");
    m_source->setAccounts({{u"me@example.com"_s, other.url(), u"me"_s}});
    sync();
    QVERIFY2(m_source->lastError().isEmpty(), qPrintable(m_source->lastError()));
    QCOMPARE(m_source->calendars().size(), 1);
    QCOMPARE(m_source->calendars().first().displayName, u"Home"_s);
    QVERIFY(week().isEmpty());
}

void TestCalDavSource::theSameServerWrittenOtherwiseKeepsItsCopies()
{
    sync();
    const QDir copies(m_dir->filePath(u"me@example.com"_s));
    const QStringList before = copies.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QVERIFY(!before.isEmpty());
    // No last slash, and the scheme in capitals: the same server.
    QUrl written = m_server->url();
    written.setPath(QString());
    written.setScheme(u"HTTP"_s);
    m_source->setAccounts({{u"me@example.com"_s, written, u"me"_s}});
    sync();
    QCOMPARE(copies.entryList(QDir::Dirs | QDir::NoDotAndDotDot), before);
    QCOMPARE(week().size(), 1);
}

void TestCalDavSource::resyncStartsOver()
{
    sync();
    // A copy gone wrong, as by hand: resync throws it away and fetches anew.
    const QDir work(m_dir->filePath(u"me@example.com/work"_s));
    QFile broken(work.filePath(u"lunch.ics"_s));
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("not a calendar");
    broken.close();
    QSignalSpy synced(m_source.get(), &CalDavSource::synced);
    QString error;
    QVERIFY2(m_source->resync(u"me@example.com"_s, &error), qPrintable(error));
    QVERIFY(synced.wait(5000));
    QCOMPARE(week().size(), 1);
    QCOMPARE(week().first().summary, u"Lunch"_s);
    QVERIFY(!m_source->resync(u"nobody@example.com"_s, &error));
}

void TestCalDavSource::guestsAddedAreForTheServerToInvite()
{
    sync();
    for (const CalendarInfo &calendar : m_source->calendars())
        QCOMPARE(calendar.invites, calendar.writable);
    QSignalSpy synced(m_source.get(), &CalDavSource::synced);
    EventEdit edit;
    edit.guests = QStringList{u"alex@example.com"_s};
    const Event lunch = week().first();
    QVERIFY(outcome([&](auto done) {
                m_source->updateEvent(lunch, edit, EditScope::ThisEvent, done);
            }).isEmpty());
    QVERIFY(synced.wait(5000));
    const QByteArray sent = m_server->calendars[FakeCalDavServer::calendarPath("work")]
                                .items.value("/dav/calendars/me/work/lunch.ics")
                                .data;
    QVERIFY(sent.toLower().contains("organizer:mailto:me@example.com"));
    QVERIFY(sent.contains("alex@example.com"));
}

void TestCalDavSource::guestsAlreadyThereAreLeftAlone()
{
    QByteArray planning = calendarText("plan", "Planning", 15);
    planning.replace("SUMMARY:", "ATTENDEE:mailto:alex@example.com\r\nSUMMARY:");
    m_server->putElsewhere("work", "plan.ics", planning);
    sync();
    QSignalSpy synced(m_source.get(), &CalDavSource::synced);
    const QList<Event> events = week();
    const auto plan = std::ranges::find(events, u"Planning"_s, &Event::summary);
    QVERIFY(plan != events.cend());
    QVERIFY(outcome([&](auto done) {
                m_source->moveEvent(*plan, plan->start.addSecs(3600), plan->end.addSecs(3600),
                                    false, done);
            }).isEmpty());
    QVERIFY(synced.wait(5000));
    const QByteArray sent = m_server->calendars[FakeCalDavServer::calendarPath("work")]
                                .items.value("/dav/calendars/me/work/plan.ics")
                                .data;
    QVERIFY(sent.contains("T160000Z"));
    QVERIFY(!sent.toLower().contains("organizer"));
}

void TestCalDavSource::noAddressNoInvitations()
{
    m_server->address.clear();
    sync();
    QVERIFY(!m_source->calendars().isEmpty());
    for (const CalendarInfo &calendar : m_source->calendars())
        QVERIFY(!calendar.invites);
}

void TestCalDavSource::aNewLookGoesToTheServer()
{
    sync();
    const QString work = calendarId(u"Work"_s);
    QString error;
    QVERIFY(m_source->setLook(work, u"Office"_s, QColor(u"#ff0066"_s), &error));
    // Shown at once, and sent with the next sync.
    QCOMPARE(calendarId(u"Office"_s), work);
    QCOMPARE(m_source->waitingChanges().size(), 1);
    sync();
    QVERIFY2(m_source->lastError().isEmpty(), qPrintable(m_source->lastError()));
    const FakeCalDavServer::Calendar &calendar =
        m_server->calendars[FakeCalDavServer::calendarPath("work")];
    QCOMPARE(calendar.name, "Office");
    QCOMPARE(calendar.color, "#FF0066FF");
    QVERIFY(m_source->waitingChanges().isEmpty());
    sync();
    QCOMPARE(calendarId(u"Office"_s), work);
    for (const CalendarInfo &info : m_source->calendars()) {
        if (info.id == work)
            QCOMPARE(info.color, QColor(u"#ff0066"_s));
    }
}

void TestCalDavSource::aLookTurnedDownComesBack()
{
    sync();
    const QString work = calendarId(u"Work"_s);
    m_server->refuseLooks = true;
    QString error;
    QVERIFY(m_source->setLook(work, u"Office"_s, {}, &error));
    sync();
    QVERIFY(!m_source->lastError().isEmpty());
    QCOMPARE(calendarId(u"Work"_s), work);
    QVERIFY(m_source->waitingChanges().isEmpty());
    QVERIFY(!m_source->setLook(u"caldav:nobody/work"_s, u"Office"_s, {}, &error));
}

void TestCalDavSource::looksGivenInSettingsBecomeTheCalendars()
{
    sync();
    const QString work = calendarId(u"Work"_s);
    Settings settings(m_dir->filePath(u"settings.ini"_s));
    m_source->followLooks(&settings);
    settings.setCalendarName(u"google-calendar"_s, u"Mine"_s);
    settings.setCalendarName(work, u"Office"_s);
    // The calendar's own name now, so nothing is left to stand over it.
    QCOMPARE(calendarId(u"Office"_s), work);
    QVERIFY(!settings.calendarLooks().contains(work));
    QVERIFY(settings.calendarLooks().contains(u"google-calendar"_s));
    sync();
    QCOMPARE(m_server->calendars[FakeCalDavServer::calendarPath("work")].name, "Office");
}

QTEST_MAIN(TestCalDavSource)
#include "tst_caldavsource.moc"
