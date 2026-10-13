#include "FakeTokenStore.h"

#include "callie/CalDavSource.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

/// CalDavSource against a real CalDAV server, Radicale, run for the test.
/// Skipped where Radicale is not installed, unless CALLIE_REQUIRE_RADICALE
/// is set, as in CI. CALLIE_RADICALE_PYTHON names a Python other than python3
/// that has it, such as a virtualenv's.
class TestRadicale : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void eventsGoBetweenTwoComputers();
    void aRenameReachesTheOtherComputer();

private:
    [[nodiscard]] std::unique_ptr<CalDavSource> computer(const QString &name);
    static void sync(CalDavSource &source);
    [[nodiscard]] static QString calendarId(const CalDavSource &source, const QString &name);
    [[nodiscard]] static QList<Event> week(const CalDavSource &source);
    [[nodiscard]] QByteArray stored() const;

    QTemporaryDir m_dir;
    QProcess m_server;
    QUrl m_url;
    QNetworkAccessManager m_network;
    FakeTokenStore m_tokens;
    std::unique_ptr<CalDavSource> m_here;
    std::unique_ptr<CalDavSource> m_there;
};

namespace {

const QDateTime kMonday(QDate(2026, 10, 5), QTime(0, 0), QTimeZone::UTC);

QString outcome(const std::function<void(CalendarSource::Created)> &call)
{
    QString error = u"no answer"_s;
    call([&error](const Outcome &result) { error = result.error; });
    return error;
}

bool write(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

} // namespace

void TestRadicale::initTestCase()
{
    // Run as a module, since Fedora's radicale command runs only as its own user.
    const QString python = qEnvironmentVariable("CALLIE_RADICALE_PYTHON", u"python3"_s);
    QProcess check;
    check.start(python, {u"-c"_s, u"import radicale"_s});
    if (!check.waitForFinished(10000) || check.exitStatus() != QProcess::NormalExit ||
        check.exitCode() != 0) {
        if (qEnvironmentVariableIsSet("CALLIE_REQUIRE_RADICALE"))
            QFAIL("Radicale is not installed");
        QSKIP("Radicale is not installed");
    }
    // A port free a moment ago, for the server to take.
    QTcpServer probe;
    QVERIFY(probe.listen(QHostAddress::LocalHost));
    const quint16 port = probe.serverPort();
    probe.close();

    const QString root = m_dir.path();
    QVERIFY(write(root + u"/users"_s, "me:app-password\n"));
    const QString config = u"[server]\nhosts = 127.0.0.1:%1\n"
                           "[auth]\ntype = htpasswd\nhtpasswd_filename = %2/users\n"
                           "htpasswd_encryption = plain\n"
                           "[rights]\ntype = owner_only\n"
                           "[storage]\nfilesystem_folder = %2/collections\n"
                           "[logging]\nlevel = warning\n"_s.arg(port)
                               .arg(root);
    QVERIFY(write(root + u"/config"_s, config.toUtf8()));
    // The user's calendar, as Radicale keeps one, since Callie makes none.
    QVERIFY(write(root + u"/collections/collection-root/me/work/.Radicale.props"_s,
                  R"({"tag": "VCALENDAR", "D:displayname": "Work",)"
                  R"( "C:supported-calendar-component-set": "VEVENT"})"));

    m_server.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    m_server.start(python, {u"-m"_s, u"radicale"_s, u"--config"_s, root + u"/config"_s});
    QVERIFY2(m_server.waitForStarted(), qPrintable(m_server.errorString()));
    QVERIFY(QTest::qWaitFor(
        [this, port] {
            if (m_server.state() == QProcess::NotRunning)
                return true;
            QTcpSocket socket;
            socket.connectToHost(QHostAddress::LocalHost, port);
            return socket.waitForConnected(200);
        },
        15000));
    QVERIFY2(m_server.state() == QProcess::Running, "Radicale stopped; its output is above");
    m_url = QUrl(u"http://127.0.0.1:%1/"_s.arg(port));
    m_tokens.secrets.insert(u"me@radicale"_s, u"app-password"_s);
}

void TestRadicale::cleanupTestCase()
{
    m_here.reset();
    m_there.reset();
    if (m_server.state() != QProcess::NotRunning) {
        m_server.terminate();
        m_server.waitForFinished(5000);
    }
}

void TestRadicale::init()
{
    m_here = computer(u"here"_s);
    m_there = computer(u"there"_s);
}

std::unique_ptr<CalDavSource> TestRadicale::computer(const QString &name)
{
    QDir(m_dir.filePath(name)).removeRecursively();
    auto source = std::make_unique<CalDavSource>(m_dir.filePath(name), &m_network, &m_tokens);
    source->setAccounts({{u"me@radicale"_s, m_url, u"me"_s}});
    return source;
}

void TestRadicale::sync(CalDavSource &source)
{
    QSignalSpy synced(&source, &CalDavSource::synced);
    source.refresh();
    QVERIFY(synced.wait(10000));
    QVERIFY2(source.lastError().isEmpty(), qPrintable(source.lastError()));
    QVERIFY(source.waitingChanges().isEmpty());
}

QString TestRadicale::calendarId(const CalDavSource &source, const QString &name)
{
    for (const CalendarInfo &calendar : source.calendars()) {
        if (calendar.displayName == name)
            return calendar.id;
    }
    return {};
}

QList<Event> TestRadicale::week(const CalDavSource &source)
{
    return source.eventsBetween(kMonday, kMonday.addDays(7), QTimeZone::UTC);
}

QByteArray TestRadicale::stored() const
{
    QByteArray all;
    QDirIterator files(m_dir.filePath(u"collections/collection-root/me/work"_s), {u"*.ics"_s},
                       QDir::Files);
    while (files.hasNext()) {
        QFile file(files.next());
        if (file.open(QIODevice::ReadOnly))
            all += file.readAll();
    }
    return all;
}

void TestRadicale::eventsGoBetweenTwoComputers()
{
    sync(*m_here);
    sync(*m_there);
    const QString work = calendarId(*m_here, u"Work"_s);
    QVERIFY(!work.isEmpty());

    EventDraft draft;
    draft.calendarId = work;
    draft.summary = u"Planning"_s;
    draft.start = kMonday.addDays(2).addSecs(9 * 3600);
    draft.end = draft.start.addSecs(3600);
    QVERIFY(outcome([&](auto done) { m_here->createEvent(draft, done); }).isEmpty());
    sync(*m_here);
    QVERIFY(stored().contains("SUMMARY:Planning"));

    // The other computer sees it, moves it, and this one sees the move.
    sync(*m_there);
    QCOMPARE(week(*m_there).size(), 1);
    const Event planning = week(*m_there).first();
    QVERIFY(outcome([&](auto done) {
                m_there->moveEvent(planning, planning.start.addSecs(3600),
                                   planning.end.addSecs(3600), false, done);
            }).isEmpty());
    sync(*m_there);
    sync(*m_here);
    QCOMPARE(week(*m_here).first().start.toUTC().time(), QTime(10, 0));

    // A file's events go up too.
    int imported = -1;
    m_here->importEvents(work,
                         "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//t//EN\r\nBEGIN:VEVENT\r\n"
                         "UID:party\r\nDTSTART:20261009T190000Z\r\nDTEND:20261009T230000Z\r\n"
                         "SUMMARY:Party\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n",
                         [&imported](int count, const QString &) { imported = count; });
    QCOMPARE(imported, 1);
    sync(*m_here);
    sync(*m_there);
    QCOMPARE(week(*m_there).size(), 2);

    // Deleted here, gone there.
    for (const Event &event : week(*m_here))
        QVERIFY(outcome([&](auto done) { m_here->deleteEvent(event, true, done); }).isEmpty());
    sync(*m_here);
    sync(*m_there);
    QVERIFY(week(*m_there).isEmpty());
    QVERIFY(!stored().contains("BEGIN:VEVENT"));
}

void TestRadicale::aRenameReachesTheOtherComputer()
{
    sync(*m_here);
    sync(*m_there);
    QString error;
    QVERIFY(
        m_here->setLook(calendarId(*m_here, u"Work"_s), u"Office"_s, QColor(u"#ff0066"_s), &error));
    sync(*m_here);
    sync(*m_there);
    QVERIFY(!calendarId(*m_there, u"Office"_s).isEmpty());
    for (const CalendarInfo &calendar : m_there->calendars())
        QCOMPARE(calendar.color, QColor(u"#ff0066"_s));
}

QTEST_GUILESS_MAIN(TestRadicale)
#include "tst_radicale.moc"
