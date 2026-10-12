#include "callie/LocalCalendars.h"

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestLocalCalendars : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void createsNamedFolders();
    void keepsEventsAsFiles();
    void showsChangesMadeElsewhere();
    void removingTrashesTheFolder();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    QString root() const { return m_dir->filePath(u"calendars"_s); }
};

namespace {

const QDateTime kMonday(QDate(2026, 10, 5), QTime(0, 0), QTimeZone::UTC);

EventDraft lunch(const QString &calendarId)
{
    EventDraft draft;
    draft.calendarId = calendarId;
    draft.summary = u"Lunch"_s;
    draft.start = kMonday.addSecs(12 * 3600);
    draft.end = kMonday.addSecs(13 * 3600);
    draft.id = u"lunch-1"_s;
    return draft;
}

QList<Event> week(const CalendarSource &source)
{
    return source.eventsBetween(kMonday, kMonday.addDays(7), QTimeZone::UTC);
}

QString outcome(const std::function<void(CalendarSource::Created)> &call)
{
    QString error = u"no answer"_s;
    call([&error](const Outcome &result) { error = result.error; });
    return error;
}

} // namespace

void TestLocalCalendars::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    // Trash goes here too, never to the desktop's.
    qputenv("XDG_DATA_HOME", m_dir->path().toUtf8());
}

void TestLocalCalendars::createsNamedFolders()
{
    LocalCalendars local(root());
    QVERIFY(local.calendars().isEmpty());
    QString error;
    const QString id = local.create(u"  Book club "_s, QColor(u"#33b679"_s), &error);
    QCOMPARE(id, u"local:book-club"_s);
    QCOMPARE(local.create(u"Book club"_s, {}, &error), u"local:book-club-2"_s);
    QVERIFY(local.create(u"   "_s, {}, &error).isEmpty());

    const QList<CalendarInfo> calendars = local.calendars();
    QCOMPARE(calendars.size(), 2);
    QCOMPARE(calendars.first().displayName, u"Book club"_s);
    QCOMPARE(calendars.first().color, QColor(u"#33b679"_s));
    QVERIFY(calendars.first().writable);
    // One without a color of its own gets one no other calendar has.
    QVERIFY(calendars.last().color.isValid());
    QVERIFY(calendars.last().color != calendars.first().color);
}

void TestLocalCalendars::keepsEventsAsFiles()
{
    LocalCalendars local(root());
    QString error;
    const QString id = local.create(u"Home"_s, {}, &error);
    QVERIFY(outcome([&](auto done) { local.createEvent(lunch(id), done); }).isEmpty());
    QVERIFY(QFile::exists(root() + u"/home/lunch-1.ics"_s));
    // Sent again after a retry, still one.
    QVERIFY(outcome([&](auto done) { local.createEvent(lunch(id), done); }).isEmpty());
    QCOMPARE(week(local).size(), 1);

    const Event event = week(local).first();
    QCOMPARE(event.summary, u"Lunch"_s);
    QVERIFY(event.canEdit);
    QVERIFY(outcome([&](auto done) {
                local.moveEvent(event, event.start.addSecs(3600), event.end.addSecs(3600), false,
                                done);
            }).isEmpty());
    QCOMPARE(week(local).first().start.time(), QTime(13, 0));

    EventEdit edit;
    edit.summary = u"Long lunch"_s;
    QVERIFY(outcome([&](auto done) {
                local.updateEvent(week(local).first(), edit, EditScope::ThisEvent, done);
            }).isEmpty());
    QCOMPARE(week(local).first().summary, u"Long lunch"_s);

    QVERIFY(
        outcome([&](auto done) { local.deleteEvent(week(local).first(), false, done); }).isEmpty());
    QVERIFY(week(local).isEmpty());
    QVERIFY(!QFile::exists(root() + u"/home/lunch-1.ics"_s));

    // Background loads read the same files.
    QVERIFY(outcome([&](auto done) { local.createEvent(lunch(id), done); }).isEmpty());
    QFuture<SourceSnapshot> load = local.load(kMonday, kMonday.addDays(7), QTimeZone::UTC);
    load.waitForFinished();
    QCOMPARE(load.result().events.size(), 1);
    QCOMPARE(load.result().calendars.size(), 1);
}

void TestLocalCalendars::showsChangesMadeElsewhere()
{
    QDir().mkpath(root() + u"/synced"_s);
    LocalCalendars local(root());
    QCOMPARE(local.calendars().size(), 1);
    QCOMPARE(local.calendars().first().displayName, u"synced"_s);

    // As vdirsyncer would: a new file, written in one go.
    QSignalSpy changed(&local, &CalendarSource::changed);
    QFile file(root() + u"/synced/a.ics"_s);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//t//EN\r\nBEGIN:VEVENT\r\nUID:a\r\n"
               "DTSTART:20261006T090000Z\r\nDTEND:20261006T100000Z\r\nSUMMARY:Dentist\r\n"
               "END:VEVENT\r\nEND:VCALENDAR\r\n");
    file.close();
    QVERIFY(changed.wait());
    QCOMPARE(week(local).first().summary, u"Dentist"_s);

    // And a calendar added beside it.
    QDir().mkpath(root() + u"/work"_s);
    QTRY_COMPARE(local.calendars().size(), 2);
}

void TestLocalCalendars::removingTrashesTheFolder()
{
    LocalCalendars local(root());
    QString error;
    const QString id = local.create(u"Old"_s, {}, &error);
    QVERIFY2(local.remove(id, &error), qPrintable(error));
    QVERIFY(local.calendars().isEmpty());
    QVERIFY(!QDir(root() + u"/old"_s).exists());
    QVERIFY(!local.remove(id, &error));
}

QTEST_MAIN(TestLocalCalendars)
#include "tst_localcalendars.moc"
