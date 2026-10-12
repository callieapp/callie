#include "ImportController.h"

#include "callie/LocalCalendars.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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

} // namespace

void TestImport::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_local = std::make_unique<LocalCalendars>(m_dir->filePath(u"calendars"_s));
    ImportController::instance()->setUp(m_local.get());
}

void TestImport::cleanup()
{
    ImportController::instance()->cancel();
    ImportController::instance()->setUp(nullptr);
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
}

QTEST_GUILESS_MAIN(TestImport)
#include "tst_import.moc"
