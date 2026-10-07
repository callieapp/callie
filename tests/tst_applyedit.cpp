#include "callie/Event.h"
#include "callie/EventEdit.h"

#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestApplyEdit : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void fieldsAndTimesOfTheEditedOne();
    void otherOccurrencesMoveByDaysToTheNewTime();
    void allDayKeepsItsDays();
    void guestsKeepTheirAnswers();
};

namespace {

const QTimeZone kBerlin("Europe/Berlin");

Event occurrence(const QString &id, int day, int hour)
{
    Event e;
    e.uid = u"swim"_s;
    e.eventId = id;
    e.seriesId = u"swim"_s;
    e.summary = u"Swim"_s;
    e.start = QDateTime(QDate(2026, 10, day), QTime(hour, 0), kBerlin).toUTC();
    e.end = e.start.addSecs(3600);
    return e;
}

} // namespace

void TestApplyEdit::fieldsAndTimesOfTheEditedOne()
{
    Event e = occurrence(u"a"_s, 6, 10);
    EventEdit edit;
    edit.summary = u"Long swim"_s;
    edit.start = QDateTime(QDate(2026, 10, 6), QTime(11, 0), kBerlin);
    edit.end = QDateTime(QDate(2026, 10, 6), QTime(12, 30), kBerlin);
    applyEdit(e, e, edit);
    QCOMPARE(e.summary, u"Long swim"_s);
    QCOMPARE(e.start, *edit.start);
    QCOMPARE(e.end, *edit.end);
    // Still shown in the zone it was shown in, and now written in Berlin's.
    QCOMPARE(e.start.offsetFromUtc(), 0);
    QCOMPARE(e.zone, u"Europe/Berlin"_s);
}

void TestApplyEdit::otherOccurrencesMoveByDaysToTheNewTime()
{
    // Tuesday's swim moved to Wednesday 7:30; Thursday's follows to Friday 7:30,
    // though summer time ends in between.
    const Event tuesday = occurrence(u"tue"_s, 20, 10);
    Event thursday = occurrence(u"thu"_s, 29, 10);
    EventEdit edit;
    edit.start = QDateTime(QDate(2026, 10, 21), QTime(7, 30), kBerlin);
    edit.end = QDateTime(QDate(2026, 10, 21), QTime(8, 30), kBerlin);
    applyEdit(thursday, tuesday, edit);
    QCOMPARE(thursday.start, QDateTime(QDate(2026, 10, 30), QTime(7, 30), kBerlin));
    QCOMPARE(thursday.end, QDateTime(QDate(2026, 10, 30), QTime(8, 30), kBerlin));
}

void TestApplyEdit::allDayKeepsItsDays()
{
    Event e = occurrence(u"a"_s, 6, 10);
    EventEdit edit;
    edit.allDay = true;
    edit.start = QDateTime(QDate(2026, 10, 6), QTime(0, 0), kBerlin);
    edit.end = QDateTime(QDate(2026, 10, 8), QTime(0, 0), kBerlin);
    applyEdit(e, e, edit);
    QVERIFY(e.allDay);
    // Two days, from midnight to midnight in the zone it is shown in.
    QCOMPARE(e.start, QDateTime(QDate(2026, 10, 6), QTime(0, 0), QTimeZone::UTC));
    QCOMPARE(e.end, QDateTime(QDate(2026, 10, 8), QTime(0, 0), QTimeZone::UTC));
}

void TestApplyEdit::guestsKeepTheirAnswers()
{
    Event e = occurrence(u"a"_s, 6, 10);
    e.guests = {{u"me@x.com"_s, u"Me"_s, u"accepted"_s, false, true},
                {u"pat@x.com"_s, u"Pat"_s, u"accepted"_s, false, false},
                {u"sam@x.com"_s, u"Sam"_s, u"declined"_s, false, false}};
    EventEdit edit;
    edit.guests = QStringList{u"pat@x.com"_s, u"alex@x.com"_s};
    applyEdit(e, e, edit);
    QCOMPARE(e.attendees, (QStringList{u"pat@x.com"_s, u"alex@x.com"_s}));
    QCOMPARE(e.guests.size(), 3);
    QVERIFY(e.guests.at(0).self);
    QCOMPARE(e.guests.at(1).response, u"accepted"_s);
    QCOMPARE(e.guests.at(2).email, u"alex@x.com"_s);
    QCOMPARE(e.guests.at(2).response, u"needsAction"_s);
}

QTEST_GUILESS_MAIN(TestApplyEdit)
#include "tst_applyedit.moc"
