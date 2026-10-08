#include "Commands.h"

#include "callie/ContactBook.h"
#include "callie/SampleSource.h"
#include "callie/Settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace callie::cli;
using namespace Qt::StringLiterals;

namespace {

/// The sample, with Dentist and the vendor call also in a second calendar
/// under the same ids, as events both calendars were invited to would be.
class SharedSource : public SampleSource
{
public:
    QList<Event> eventsBetween(const QDateTime &from, const QDateTime &to,
                               const QTimeZone &tz) const override
    {
        QList<Event> events = SampleSource::eventsBetween(from, to, tz);
        const qsizetype count = events.size();
        for (qsizetype i = 0; i < count; ++i) {
            if (events.at(i).summary == u"Dentist" || events.at(i).summary == u"Vendor call") {
                Event copy = events.at(i);
                copy.calendarId = copy.calendarId == u"work" ? u"personal"_s : u"work"_s;
                events.append(copy);
            }
        }
        return events;
    }
};

const QDateTime kNow(QDate(2026, 10, 7), QTime(13, 40), QTimeZone::UTC);

QDateTime at(int day, int hour, int minute = 0)
{
    return QDateTime(QDate(2026, 10, day), QTime(hour, minute), QTimeZone::UTC);
}

} // namespace

class TestCli : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void agendaGivesIdsToScripts();
    void searchListsASeriesOnce();
    void invitesWaitingAnswerOnce();
    void addGoesInTheDefaultCalendar();
    void editMovesAndKeepsTheLength();
    void editAllDayEndsOnItsLastDay();
    void repeatIsForTheWholeSeries();
    void mistakesAreReportedNotMade();
    void respondAndDelete();
    void duplicateKeepsTheLength();
    void whenReadsDaysAndTimes();
    void contactsFindGuestsAndContacts();
    void idsNameTheirCalendar();
    void allDayKeepsItsDaysAcrossAClockChange();
    void timedKeepsTheEndGiven();
    void sharedIdsNeedTheirCalendar();
    void settingsReadAndChange();
    void calendarsAndAccountsTakeNewLooks();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<SampleSource> m_source;
    QString m_out;
    QString m_err;
    std::unique_ptr<QTextStream> m_outStream;
    std::unique_ptr<QTextStream> m_errStream;
    std::unique_ptr<Commands> m_commands;

    /// Runs a command that answers through `done`, and gives its exit code.
    int run(const std::function<void(const Commands::Done &)> &command)
    {
        int code = -1;
        command([&code](int c) { code = c; });
        m_outStream->flush();
        m_errStream->flush();
        return code;
    }

    Event named(const QString &summary, QDate day)
    {
        const QDateTime from(day, QTime(0, 0), QTimeZone::UTC);
        for (const Event &e : m_source->eventsBetween(from, from.addDays(1), QTimeZone::UTC)) {
            if (e.summary == summary)
                return e;
        }
        return {};
    }
};

void TestCli::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_settings = std::make_unique<Settings>(m_dir->filePath(u"settings.ini"_s));
    m_source = std::make_unique<SampleSource>();
    m_out.clear();
    m_err.clear();
    m_outStream = std::make_unique<QTextStream>(&m_out);
    m_errStream = std::make_unique<QTextStream>(&m_err);
    m_commands = std::make_unique<Commands>(*m_source, *m_settings, *m_outStream, *m_errStream,
                                            kNow, QTimeZone::UTC);
}

void TestCli::agendaGivesIdsToScripts()
{
    QCOMPARE(m_commands->agenda(1, true), 0);
    m_outStream->flush();
    const QJsonArray events = QJsonDocument::fromJson(m_out.toUtf8()).array();
    QVERIFY(!events.isEmpty());
    bool dentist = false;
    for (const QJsonValue &e : events) {
        // Only today's.
        QVERIFY(e[u"start"].toString().startsWith(u"2026-10-07"_s));
        if (e[u"title"].toString() == u"Dentist") {
            dentist = true;
            QCOMPARE(e[u"id"].toString(), u"personal/sample-3-16-0-2026-10-07"_s);
            QCOMPARE(e[u"start"].toString(), u"2026-10-07T16:00:00Z"_s);
            QVERIFY(!e[u"repeats"].toBool());
        }
    }
    QVERIFY(dentist);
}

void TestCli::searchListsASeriesOnce()
{
    QCOMPARE(m_commands->search(u"standup"_s, true), 0);
    m_outStream->flush();
    const QJsonArray found = QJsonDocument::fromJson(m_out.toUtf8()).array();
    // The standups are one series, listed once by the next of them.
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.first()[u"start"].toString(), u"2026-10-08T09:30:00Z"_s);

    // Finding nothing is not an error, and says so off stdout.
    m_out.clear();
    QCOMPARE(m_commands->search(u"standup nowhere"_s, false), 0);
    m_outStream->flush();
    QVERIFY(m_out.isEmpty());
    QCOMPARE(m_commands->search(u" "_s, false), 2);
}

void TestCli::invitesWaitingAnswerOnce()
{
    QCOMPARE(m_commands->invites(false), 0);
    m_outStream->flush();
    QCOMPARE(m_out.count(u"Vendor call"_s), 1);
    QCOMPARE(m_out.count(u"Berlin sync"_s), 1);
    QVERIFY(!m_out.contains(u"Retro"_s));
}

void TestCli::addGoesInTheDefaultCalendar()
{
    m_settings->setDefaultCalendar(u"personal"_s);
    QCOMPARE(run([this](auto done) { m_commands->add(u"Pottery friday 6-8pm"_s, {}, done); }), 0);
    const Event pottery = named(u"Pottery"_s, QDate(2026, 10, 9));
    QCOMPARE(pottery.calendarId, u"personal"_s);
    QCOMPARE(pottery.start, at(9, 18));
    // Quiet on success.
    QVERIFY(m_out.isEmpty());

    // A calendar by name, and one that is not there.
    QCOMPARE(run([this](auto done) { m_commands->add(u"Gym saturday 9am"_s, u"focus"_s, done); }),
             0);
    QCOMPARE(named(u"Gym"_s, QDate(2026, 10, 10)).calendarId, u"focus"_s);
    QCOMPARE(run([this](auto done) { m_commands->add(u"Gym sunday 9am"_s, u"Nope"_s, done); }), 1);
    QVERIFY(m_err.contains(u"Nope"_s));
}

void TestCli::editMovesAndKeepsTheLength()
{
    const QString id = u"sample-3-16-0-2026-10-07"_s;
    EditOptions options;
    options.start = u"friday 3pm"_s;
    options.where = u"New clinic"_s;
    QCOMPARE(run([&](auto done) { m_commands->edit(id, options, {}, done); }), 0);
    const Event moved = named(u"Dentist"_s, QDate(2026, 10, 9));
    QCOMPARE(moved.start, at(9, 15));
    QCOMPARE(moved.end, at(9, 16));
    QCOMPARE(moved.location, u"New clinic"_s);
}

void TestCli::editAllDayEndsOnItsLastDay()
{
    EditOptions options;
    options.allDay = true;
    options.start = u"2026-10-08"_s;
    options.end = u"2026-10-09"_s;
    QCOMPARE(
        run([&](auto done) { m_commands->edit(u"sample-3-16-0-2026-10-07"_s, options, {}, done); }),
        0);
    const Event trip = named(u"Dentist"_s, QDate(2026, 10, 8));
    QVERIFY(trip.allDay);
    // --json gives the last day back, as --end took it.
    QCOMPARE(m_commands->agenda(3, true), 0);
    m_outStream->flush();
    bool listed = false;
    for (const QJsonValue &e : QJsonDocument::fromJson(m_out.toUtf8()).array()) {
        if (e[u"title"].toString() == u"Dentist" && e[u"allDay"].toBool()) {
            listed = true;
            QCOMPARE(e[u"start"].toString(), u"2026-10-08"_s);
            QCOMPARE(e[u"end"].toString(), u"2026-10-09"_s);
        }
    }
    QVERIFY(listed);
    QCOMPARE(trip.start.date(), QDate(2026, 10, 8));
    QCOMPARE(trip.end.date(), QDate(2026, 10, 10));
}

void TestCli::repeatIsForTheWholeSeries()
{
    // The Wednesday standup is a call, so a series.
    const QString id = u"sample-3-9-30-2026-10-07"_s;
    EditOptions options;
    options.repeat = u"daily"_s;
    QCOMPARE(run([&](auto done) { m_commands->edit(id, options, {}, done); }), 2);
    QVERIFY(m_err.contains(u"--scope"_s));
    options.repeat = u"fortnightly"_s;
    QCOMPARE(run([&](auto done) { m_commands->edit(id, options, u"all"_s, done); }), 2);

    EditOptions rename;
    rename.title = u"Daily"_s;
    QCOMPARE(run([&](auto done) { m_commands->edit(id, rename, u"all"_s, done); }), 0);
    QCOMPARE(named(u"Daily"_s, QDate(2026, 10, 14)).summary, u"Daily"_s);
}

void TestCli::mistakesAreReportedNotMade()
{
    QCOMPARE(run([this](auto done) { m_commands->edit(u"nope"_s, {}, {}, done); }), 1);
    QVERIFY(m_err.contains(u"--json"_s));
    const QString id = u"sample-3-16-0-2026-10-07"_s;
    QCOMPARE(run([&](auto done) { m_commands->edit(id, {}, {}, done); }), 2);
    EditOptions backwards;
    backwards.start = u"17:00"_s;
    backwards.end = u"16:00"_s;
    QCOMPARE(run([&](auto done) { m_commands->edit(id, backwards, {}, done); }), 2);
    EditOptions unreadable;
    unreadable.start = u"someday"_s;
    QCOMPARE(run([&](auto done) { m_commands->edit(id, unreadable, {}, done); }), 2);
    QCOMPARE(run([&](auto done) { m_commands->edit(id, {}, u"sometimes"_s, done); }), 2);
    QCOMPARE(named(u"Dentist"_s, QDate(2026, 10, 7)).start, at(7, 16));
}

void TestCli::respondAndDelete()
{
    const QString vendor = u"sample-3-10-30-2026-10-07"_s;
    QCOMPARE(run([&](auto done) { m_commands->respond(vendor, u"maybe"_s, {}, done); }), 0);
    QCOMPARE(named(u"Vendor call"_s, QDate(2026, 10, 7)).responseStatus, u"tentative"_s);
    QCOMPARE(run([&](auto done) { m_commands->respond(vendor, u"perhaps"_s, {}, done); }), 2);
    // Not an invitation.
    QCOMPARE(run([&](auto done) {
                 m_commands->respond(u"sample-3-16-0-2026-10-07"_s, u"yes"_s, {}, done);
             }),
             1);

    QCOMPARE(run([&](auto done) { m_commands->remove(vendor, {}, done); }), 0);
    QVERIFY(named(u"Vendor call"_s, QDate(2026, 10, 7)).summary.isEmpty());
    QCOMPARE(run([&](auto done) {
                 m_commands->remove(u"sample-3-16-0-2026-10-07"_s, u"following"_s, done);
             }),
             2);
}

void TestCli::duplicateKeepsTheLength()
{
    QCOMPARE(run([this](auto done) {
                 m_commands->duplicate(u"sample-3-16-0-2026-10-07"_s, u"2026-10-08 10:00"_s, done);
             }),
             0);
    const Event copy = named(u"Dentist"_s, QDate(2026, 10, 8));
    QCOMPARE(copy.start, at(8, 10));
    QCOMPARE(copy.end, at(8, 11));
    QCOMPARE(copy.location, u"Clinic"_s);
}

void TestCli::whenReadsDaysAndTimes()
{
    const QDate day(2026, 10, 7);
    QCOMPARE(*m_commands->when(u"2026-10-08 15:00"_s, day), at(8, 15));
    QCOMPARE(*m_commands->when(u"2026-10-08T15:30"_s, day), at(8, 15, 30));
    QCOMPARE(*m_commands->when(u"2026-10-08"_s, day), at(8, 0));
    QCOMPARE(*m_commands->when(u"9:15"_s, day), at(7, 9, 15));
    QCOMPARE(*m_commands->when(u"tomorrow 3pm"_s, day), at(8, 15));
    QVERIFY(!m_commands->when(u"someday"_s, day));
    QCOMPARE(*m_commands->when(u"2026-10-08T15:00:00"_s, day), at(8, 15));
    // A date with words after it is read as words, not cut to midnight.
    QCOMPARE(*m_commands->when(u"2026-10-08 3pm"_s, day), at(8, 15));
}

void TestCli::idsNameTheirCalendar()
{
    EditOptions rename;
    rename.title = u"Orthodontist"_s;
    QCOMPARE(run([&](auto done) {
                 m_commands->edit(u"work/sample-3-16-0-2026-10-07"_s, rename, {}, done);
             }),
             1);
    QCOMPARE(run([&](auto done) {
                 m_commands->edit(u"personal/sample-3-16-0-2026-10-07"_s, rename, {}, done);
             }),
             0);
    QCOMPARE(named(u"Orthodontist"_s, QDate(2026, 10, 7)).calendarId, u"personal"_s);
}

void TestCli::allDayKeepsItsDaysAcrossAClockChange()
{
    // New York springs forward on 8 March 2026, a 23-hour day.
    const QTimeZone york("America/New_York");
    Commands commands(*m_source, *m_settings, *m_outStream, *m_errStream,
                      QDateTime(QDate(2026, 3, 1), QTime(12, 0), york), york);
    QCOMPARE(run([&](auto done) { commands.add(u"Trip 2026-03-05"_s, {}, done); }), 0);
    const QDateTime from(QDate(2026, 3, 5), QTime(0, 0), york);
    QString id;
    for (const Event &e : m_source->eventsBetween(from, from.addDays(1), york)) {
        if (e.summary == u"Trip")
            id = e.calendarId + u'/' + e.eventId;
    }
    QVERIFY(!id.isEmpty());
    EditOptions options;
    options.start = u"2026-03-08"_s;
    QCOMPARE(run([&](auto done) { commands.edit(id, options, {}, done); }), 0);
    const QDateTime moved(QDate(2026, 3, 8), QTime(0, 0), york);
    for (const Event &e : m_source->eventsBetween(moved, moved.addDays(1), york)) {
        if (e.summary == u"Trip") {
            QCOMPARE(e.start.date(), QDate(2026, 3, 8));
            QCOMPARE(e.end.toTimeZone(york).date(), QDate(2026, 3, 9));
            return;
        }
    }
    QFAIL("the trip did not move");
}

void TestCli::timedKeepsTheEndGiven()
{
    // Dentist made all day, then given times again with only an end.
    const QString id = u"personal/sample-3-16-0-2026-10-07"_s;
    EditOptions allDay;
    allDay.allDay = true;
    QCOMPARE(run([&](auto done) { m_commands->edit(id, allDay, {}, done); }), 0);
    EditOptions timed;
    timed.allDay = false;
    timed.end = u"11:30"_s;
    QCOMPARE(run([&](auto done) { m_commands->edit(id, timed, {}, done); }), 0);
    const Event dentist = named(u"Dentist"_s, QDate(2026, 10, 7));
    QVERIFY(!dentist.allDay);
    QCOMPARE(dentist.start, at(7, 9));
    QCOMPARE(dentist.end, at(7, 11, 30));
}

void TestCli::sharedIdsNeedTheirCalendar()
{
    SharedSource shared;
    Commands commands(shared, *m_settings, *m_outStream, *m_errStream, kNow, QTimeZone::UTC);
    QCOMPARE(run([&](auto done) { commands.remove(u"sample-3-16-0-2026-10-07"_s, {}, done); }), 1);
    QVERIFY(m_err.contains(u"more than one calendar"_s));

    // An invitation in two calendars is listed in each, so either can be answered.
    m_out.clear();
    QCOMPARE(commands.invites(true), 0);
    m_outStream->flush();
    QCOMPARE(m_out.count(u"\"Vendor call\""_s), 2);

    QCOMPARE(run([&](auto done) { commands.remove(u"work/sample-3-16-0-2026-10-07"_s, {}, done); }),
             0);
}

void TestCli::settingsReadAndChange()
{
    QCOMPARE(Commands::settings(*m_settings, *m_outStream, *m_errStream, {}, {}), 0);
    m_outStream->flush();
    QVERIFY(m_out.contains(u"viMode\tfalse\n"_s));
    QVERIFY(m_out.contains(u"timeFormat\tLocale\n"_s));
    // Callie's own bookkeeping is not for the user.
    QVERIFY(!m_out.contains(u"lastSeenVersion"_s));

    m_out.clear();
    QCOMPARE(Commands::settings(*m_settings, *m_outStream, *m_errStream, u"viMode"_s, u"on"_s), 0);
    QVERIFY(m_settings->viMode());
    m_outStream->flush();
    QVERIFY(m_out.isEmpty());
    QCOMPARE(Commands::settings(*m_settings, *m_outStream, *m_errStream, u"timeFormat"_s,
                                u"TwelveHour"_s),
             0);
    QCOMPARE(m_settings->timeFormat(), Settings::TimeFormat::TwelveHour);
    QCOMPARE(
        Commands::settings(*m_settings, *m_outStream, *m_errStream, u"leaderTimeout"_s, u"1500"_s),
        0);
    QCOMPARE(m_settings->leaderTimeout(), 1500);

    // What a setting refuses, or cannot read, is a mistake.
    for (const auto &[name, value] :
         {std::pair(u"leaderKey"_s, u"x"_s), std::pair(u"viMode"_s, u"maybe"_s),
          std::pair(u"timeFormat"_s, u"Sundial"_s), std::pair(u"lastSeenVersion"_s, u"9"_s),
          std::pair(u"nope"_s, u"1"_s)})
        QCOMPARE(Commands::settings(*m_settings, *m_outStream, *m_errStream, name, value), 2);
    QCOMPARE(m_settings->leaderKey(), u","_s);

    // A zone or time a setting would change into something else is refused,
    // and what was there stays.
    m_settings->setTimeZoneId(u"Europe/Berlin"_s);
    QCOMPARE(Commands::settings(*m_settings, *m_outStream, *m_errStream, u"timeZoneId"_s,
                                u"Nowhere/Nope"_s),
             2);
    QCOMPARE(m_settings->timeZoneId(), u"Europe/Berlin"_s);
    QCOMPARE(Commands::settings(*m_settings, *m_outStream, *m_errStream, u"workStart"_s, u"5000"_s),
             2);
    QCOMPARE(m_settings->workStart(), 9 * 60);
    QCOMPARE(m_settings->workEnd(), 17 * 60);
    QCOMPARE(Commands::settings(*m_settings, *m_outStream, *m_errStream, u"workEnd"_s, u"0"_s), 2);
    QCOMPARE(m_settings->workStart(), 9 * 60);
    QCOMPARE(m_settings->workEnd(), 17 * 60);
    // An empty zone means the system's, and is taken.
    QCOMPARE(Commands::settings(*m_settings, *m_outStream, *m_errStream, u"timeZoneId"_s,
                                std::optional(QString())),
             0);
    QVERIFY(m_settings->timeZoneId().isEmpty());
}

void TestCli::calendarsAndAccountsTakeNewLooks()
{
    QCOMPARE(m_commands->calendarLook(u"rename"_s, u"Focus"_s, u"Deep work"_s), 0);
    QCOMPARE(m_settings->calendarLooks().value(u"focus"_s).toMap().value(u"name"_s).toString(),
             u"Deep work"_s);
    QCOMPARE(m_commands->calendarLook(u"color"_s, u"focus"_s, u"#e86a92"_s), 0);
    QCOMPARE(m_commands->calendarLook(u"color"_s, u"focus"_s, u"pinkish"_s), 2);
    QCOMPARE(m_commands->calendarLook(u"hide"_s, u"Personal"_s, {}), 0);
    QVERIFY(m_settings->hiddenCalendars().contains(u"personal"_s));
    QCOMPARE(m_commands->calendarLook(u"show"_s, u"personal"_s, {}), 0);
    QVERIFY(m_settings->hiddenCalendars().isEmpty());
    QCOMPARE(m_commands->calendarLook(u"reset"_s, u"focus"_s, {}), 0);
    QVERIFY(m_settings->calendarLooks().isEmpty());
    QCOMPARE(m_commands->calendarLook(u"hide"_s, u"Nope"_s, {}), 1);
    // A name two calendars share needs the id instead.
    {
        // Names as the sidebar shows them, which the source reads through its looks.
        class Named : public SampleSource
        {
        public:
            QList<CalendarInfo> calendars() const override
            {
                QList<CalendarInfo> list = SampleSource::calendars();
                for (CalendarInfo &c : list) {
                    if (c.id != u"personal")
                        c.displayName = u"Shared"_s;
                }
                return list;
            }
        } named;
        Commands commands(named, *m_settings, *m_outStream, *m_errStream, kNow, QTimeZone::UTC);
        QCOMPARE(commands.calendarLook(u"hide"_s, u"shared"_s, {}), 2);
        QVERIFY(m_err.contains(u"work, focus"_s));
        QVERIFY(m_settings->hiddenCalendars().isEmpty());
        QCOMPARE(commands.calendarLook(u"hide"_s, u"work"_s, {}), 0);
    }
    m_settings->setCalendarVisible(u"work"_s, true);
    QCOMPARE(m_commands->calendarLook(u"paint"_s, u"focus"_s, {}), 2);
    QCOMPARE(m_commands->calendarLook(u"list"_s, {}, {}), 2);
    // Quiet on success: only what was asked for goes to stdout.
    m_outStream->flush();
    QVERIFY(m_out.isEmpty());

    QCOMPARE(m_commands->renameAccount(u"sam@work.example"_s, u"Work"_s), 0);
    QCOMPARE(m_settings->accountName(u"sam@work.example"_s), u"Work"_s);
    QCOMPARE(m_commands->renameAccount(u"nobody@example.com"_s, u"Nobody"_s), 1);
}

void TestCli::contactsFindGuestsAndContacts()
{
    ContactBook book({});
    book.add(u"me@example.com"_s, {{u"Priya Rao"_s, u"prao@example.org"_s}});
    QCOMPARE(m_commands->contacts(book, u"priya"_s, false), 0);
    m_outStream->flush();
    // The sample's Priya from her events, and the contact.
    QCOMPARE(m_out, u"priya@example.com\tPriya\nprao@example.org\tPriya Rao\n"_s);
    QCOMPARE(m_commands->contacts(book, {}, false), 2);
}

QTEST_GUILESS_MAIN(TestCli)
#include "tst_cli.moc"
