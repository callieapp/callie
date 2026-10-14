#include "ExportController.h"
#include "ImportController.h"

#include "callie/CombinedSource.h"
#include "callie/LocalCalendars.h"
#include "callie/SampleSource.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace callie;
using namespace Qt::StringLiterals;

class TestImport : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();
    void showsWhatIsInTheFile();
    void addsToACalendarOrANewOne();
    void turnsAwayWhatIsNotACalendar();
    void allDayEventsShowTheirOwnDay();
    void goesToCalendarsElsewhereToo();
    void savesACalendarOrAnEvent();

private:
    QString write(const QString &name, const QByteArray &data)
    {
        const QString path = m_dir->filePath(name);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
            file.write(data);
        return path;
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<LocalCalendars> m_local;
};

namespace {

const QByteArray kParty = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//t//EN\r\n"
                          "BEGIN:VEVENT\r\nUID:late\r\nDTSTART:20261020T180000Z\r\n"
                          "DTEND:20261020T200000Z\r\nSUMMARY:Afterparty\r\nEND:VEVENT\r\n"
                          "BEGIN:VEVENT\r\nUID:early\r\nDTSTART:20261020T120000Z\r\n"
                          "DTEND:20261020T130000Z\r\nSUMMARY:Lunch\r\nEND:VEVENT\r\n"
                          "END:VCALENDAR\r\n";

/// Sample calendars that take files a moment later, as a server would.
class Remote : public SampleSource
{
public:
    void importEvents(const QString &calendarId, const QByteArray &data, Imported done) override
    {
        sent.append({calendarId, data});
        QTimer::singleShot(0, this, [this, done] { done(taken, error); });
    }
    QList<std::pair<QString, QByteArray>> sent;
    int taken = 2;
    QString error;
};

} // namespace

void TestImport::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_local = std::make_unique<LocalCalendars>(m_dir->filePath(u"calendars"_s));
    ImportController::instance()->setUp(m_local.get(), m_local.get());
}

void TestImport::cleanup()
{
    ImportController::instance()->cancel();
    ImportController::instance()->setUp(nullptr, nullptr);
}

void TestImport::showsWhatIsInTheFile()
{
    ImportController *importer = ImportController::instance();
    QSignalSpy pending(importer, &ImportController::pendingChanged);
    QVERIFY(importer->open({QUrl::fromLocalFile(write(u"party.ics"_s, kParty))}));
    QVERIFY(importer->pending());
    QCOMPARE(pending.size(), 1);
    QCOMPARE(importer->fileName(), u"party.ics"_s);
    QCOMPARE(importer->count(), 2);
    // Soonest first.
    QCOMPARE(importer->preview().first().toMap().value(u"summary"_s).toString(), u"Lunch"_s);
    // With no calendar yet, only a new one named after the file.
    const QVariantList calendars = importer->calendars();
    QCOMPARE(calendars.size(), 1);
    QCOMPARE(calendars.first().toMap().value(u"id"_s).toString(), u"new"_s);
    QVERIFY(calendars.first().toMap().value(u"name"_s).toString().contains(u"party"_s));
}

void TestImport::addsToACalendarOrANewOne()
{
    ImportController *importer = ImportController::instance();
    QSignalSpy added(importer, &ImportController::added);
    QVERIFY(importer->open({QUrl::fromLocalFile(write(u"party.ics"_s, kParty))}));
    QVERIFY(importer->add(u"new"_s));
    QVERIFY(!importer->pending());
    QCOMPARE(added.size(), 1);
    QCOMPARE(added.first().at(0).toInt(), 2);
    QCOMPARE(added.first().at(1).toString(), u"party"_s);
    QCOMPARE(m_local->calendars().size(), 1);

    // Into one that is there, offered before making another.
    QString error;
    const QString home = m_local->create(u"Home"_s, {}, &error);
    QVERIFY(importer->open({QUrl::fromLocalFile(write(u"party.ics"_s, kParty))}));
    QCOMPARE(importer->calendars().size(), 3);
    QVERIFY(importer->add(home));
    const QDateTime day(QDate(2026, 10, 20), QTime(0, 0), QTimeZone::UTC);
    QCOMPARE(m_local->eventsBetween(day, day.addDays(1), QTimeZone::UTC).size(), 4);
}

void TestImport::turnsAwayWhatIsNotACalendar()
{
    ImportController *importer = ImportController::instance();
    QVERIFY(!ImportController::accepts(QUrl(u"https://example.com/a.ics"_s)));
    QVERIFY(!importer->open({QUrl::fromLocalFile(write(u"notes.txt"_s, "hello"))}));
    QVERIFY(importer->error().isEmpty());
    QVERIFY(!importer->open({QUrl::fromLocalFile(write(u"broken.ics"_s, "hello"))}));
    QVERIFY(!importer->error().isEmpty());
    QVERIFY(!importer->pending());
    // What is not a calendar file says nothing, nor repeats the last error.
    QVERIFY(!importer->open({QUrl::fromLocalFile(write(u"notes.txt"_s, "hello"))}));
    QVERIFY(importer->error().isEmpty());
}

void TestImport::allDayEventsShowTheirOwnDay()
{
    // Run west of UTC too, where a UTC midnight would read as the day before.
    ImportController *importer = ImportController::instance();
    QVERIFY(importer->open({QUrl::fromLocalFile(write(
        u"holiday.ics"_s, "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//t//EN\r\nBEGIN:VEVENT\r\n"
                          "UID:h\r\nDTSTART;VALUE=DATE:20261012\r\nSUMMARY:Holiday\r\n"
                          "END:VEVENT\r\nEND:VCALENDAR\r\n"))}));
    const QDateTime start = importer->preview().first().toMap().value(u"start"_s).toDateTime();
    QCOMPARE(start, QDateTime(QDate(2026, 10, 12), QTime(0, 0)));
    QCOMPARE(start.toLocalTime().date(), QDate(2026, 10, 12));
}

void TestImport::goesToCalendarsElsewhereToo()
{
    Remote remote;
    CombinedSource combined;
    combined.setSources({m_local.get(), &remote});
    ImportController *importer = ImportController::instance();
    importer->setUp(&combined, m_local.get());
    QSignalSpy added(importer, &ImportController::added);
    QVERIFY(importer->open({QUrl::fromLocalFile(write(u"party.ics"_s, kParty))}));
    const QVariantList calendars = importer->calendars();
    QVERIFY(std::ranges::any_of(calendars, [](const QVariant &c) {
        return c.toMap().value(u"id"_s).toString() == u"work";
    }));

    // Turned down part way: what went in is said, and the file stays to try again.
    remote.taken = 1;
    remote.error = u"Server said no"_s;
    QVERIFY(importer->add(u"work"_s));
    QVERIFY(importer->busy());
    QTRY_VERIFY(!importer->busy());
    QVERIFY(importer->error().contains(u"Server said no"_s));
    QVERIFY(importer->pending());
    QCOMPARE(added.size(), 0);

    remote.taken = 2;
    remote.error.clear();
    QVERIFY(importer->add(u"work"_s));
    QTRY_COMPARE(added.size(), 1);
    QCOMPARE(added.first().at(0).toInt(), 2);
    QVERIFY(!importer->pending());
    QCOMPARE(remote.sent.size(), 2);
    QCOMPARE(remote.sent.last().second, kParty);
}

void TestImport::savesACalendarOrAnEvent()
{
    QCOMPARE(ExportController::fileNameFor(u"Book club!"_s), u"book-club.ics"_s);
    QCOMPARE(ExportController::fileNameFor(u"..."_s), u"calendar.ics"_s);

    QString error;
    const QString home = m_local->create(u"Home"_s, {}, &error);
    QCOMPARE(m_local->importEvents(home, ics::parse(kParty), &error), 2);
    ExportController *exporter = ExportController::instance();
    exporter->setUp(m_local.get());
    QSignalSpy saved(exporter, &ExportController::saved);
    QSignalSpy failed(exporter, &ExportController::failed);
    const QString path = m_dir->filePath(u"lunch.ics"_s);
    QVERIFY(exporter->save(home, u"early"_s, QUrl::fromLocalFile(path)));
    QCOMPARE(saved.first().first().toString(), u"lunch.ics"_s);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const ics::Group events = ics::parse(file.readAll());
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.first()->summary(), u"Lunch"_s);

    QVERIFY(!exporter->save(u"local:gone"_s, {}, QUrl::fromLocalFile(path)));
    QCOMPARE(failed.size(), 1);
    exporter->setUp(nullptr);
}

QTEST_GUILESS_MAIN(TestImport)
#include "tst_import.moc"
