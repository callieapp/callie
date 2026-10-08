#include "Commands.h"

#include "callie/SampleSource.h"
#include "callie/Settings.h"

#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace callie::cli;
using namespace Qt::StringLiterals;

namespace {

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
    const QStringList lines = m_out.split(u'\n', Qt::SkipEmptyParts);
    QVERIFY(lines.contains(u"sample-3-16-0-2026-10-07\t2026-10-07 16:00\tDentist"_s));
    // Only today's.
    for (const QString &line : lines)
        QVERIFY2(line.section(u'\t', 1).startsWith(u"2026-10-07"_s), qPrintable(line));
}

void TestCli::searchListsASeriesOnce()
{
    QCOMPARE(m_commands->search(u"standup"_s, true), 0);
    m_outStream->flush();
    const QStringList lines = m_out.split(u'\n', Qt::SkipEmptyParts);
    // The standups are one series, listed once by the next of them.
    QCOMPARE(lines.size(), 1);
    QVERIFY(lines.first().contains(u"2026-10-08 09:30"_s));

    QCOMPARE(m_commands->search(u"standup nowhere"_s, false), 1);
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
    QVERIFY(m_out.contains(u"Added Pottery"_s));

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
    QVERIFY(m_out.contains(u"Changed Dentist"_s));
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
    QVERIFY(m_err.contains(u"--ids"_s));
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
}

QTEST_GUILESS_MAIN(TestCli)
#include "tst_cli.moc"
