#include "callie/ReminderScheduler.h"

#include <QSignalSpy>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

/// Hands out a fixed list of events.
class ListSource : public CalendarSource
{
public:
    QList<Event> events;

    QString sourceId() const override { return u"list"_s; }
    QList<CalendarInfo> calendars() const override { return {}; }
    // All-day events start at midnight in the zone asked for, as real sources do.
    QList<Event> eventsBetween(const QDateTime &from, const QDateTime &to,
                               const QTimeZone &tz) const override
    {
        QList<Event> result;
        for (Event event : events) {
            if (event.allDay) {
                event.start = QDateTime(event.start.date(), QTime(0, 0), tz);
                event.end = QDateTime(event.end.date(), QTime(0, 0), tz);
            }
            if (event.start < to && event.end > from)
                result.append(event);
        }
        return result;
    }
    void refresh() override {}
};

QDateTime at(int hour, int minute = 0)
{
    return QDateTime(QDate(2026, 10, 7), QTime(hour, minute), QTimeZone::UTC);
}

Event eventAt(const QString &uid, const QDateTime &start)
{
    Event result;
    result.uid = uid;
    result.calendarId = u"cal"_s;
    result.summary = uid;
    result.start = start;
    result.end = start.addSecs(3600);
    return result;
}

} // namespace

class TestReminderScheduler : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void defaultReminderIsSaidOnce();
    void workPlacesAreNotReminded();
    void ownRemindersWinOverTheDefault();
    void eachReminderIsSaid();
    void nothingForDeclinedHiddenOrAllDay();
    void allDayRemindersFollowTheZone();
    void recentReminderIsSaidOnStart();
    void endedEventsAreNotCaughtUp();
    void snoozeRemindsAgain();
    void stoppedSaysNothing();

private:
    void stepTo(const QDateTime &moment);

    ListSource m_source;
    std::unique_ptr<ReminderScheduler> m_scheduler;
    QDateTime m_now;
    QList<QPair<QString, int>> m_said;
};

void TestReminderScheduler::init()
{
    m_source.events.clear();
    m_said.clear();
    m_now = at(9);
    m_scheduler = std::make_unique<ReminderScheduler>();
    m_scheduler->setNow([this] { return m_now; });
    m_scheduler->setSource(&m_source);
    connect(m_scheduler.get(), &ReminderScheduler::due, this,
            [this](const Event &e, int minutes) { m_said.append({e.uid, minutes}); });
}

void TestReminderScheduler::stepTo(const QDateTime &moment)
{
    m_now = moment;
    m_scheduler->check();
}

void TestReminderScheduler::defaultReminderIsSaidOnce()
{
    m_source.events = {eventAt(u"standup"_s, at(10))};
    m_scheduler->setEnabled(true);
    stepTo(at(9, 49));
    QVERIFY(m_said.isEmpty());
    stepTo(at(9, 50));
    QCOMPARE(m_said, (QList<QPair<QString, int>>{{u"standup"_s, 10}}));
    stepTo(at(9, 55));
    Q_EMIT m_source.changed();
    QCOMPARE(m_said.size(), 1);
}

void TestReminderScheduler::workPlacesAreNotReminded()
{
    // Where the user works that day is no event to be reminded of.
    Event office = eventAt(u"office"_s, at(10));
    office.workPlace = true;
    m_source.events = {office};
    m_scheduler->setEnabled(true);
    stepTo(at(9, 55));
    QVERIFY(m_said.isEmpty());
}

void TestReminderScheduler::ownRemindersWinOverTheDefault()
{
    Event quiet = eventAt(u"quiet"_s, at(10));
    quiet.remindersKnown = true;
    Event early = eventAt(u"early"_s, at(10));
    early.remindersKnown = true;
    early.reminders = {30};
    m_source.events = {quiet, early};
    m_scheduler->setEnabled(true);
    stepTo(at(9, 59));
    QCOMPARE(m_said, (QList<QPair<QString, int>>{{u"early"_s, 30}}));
}

void TestReminderScheduler::eachReminderIsSaid()
{
    Event talk = eventAt(u"talk"_s, at(10));
    talk.remindersKnown = true;
    talk.reminders = {15, 0};
    m_source.events = {talk};
    m_scheduler->setEnabled(true);
    stepTo(at(9, 45));
    stepTo(at(10));
    QCOMPARE(m_said, (QList<QPair<QString, int>>{{u"talk"_s, 15}, {u"talk"_s, 0}}));
}

void TestReminderScheduler::nothingForDeclinedHiddenOrAllDay()
{
    Event declined = eventAt(u"declined"_s, at(10));
    declined.declined = true;
    Event hidden = eventAt(u"hidden"_s, at(10));
    hidden.calendarId = u"other"_s;
    Event holiday =
        eventAt(u"holiday"_s, QDateTime(QDate(2026, 10, 8), QTime(0, 0), QTimeZone::UTC));
    holiday.allDay = true;
    holiday.end = holiday.start.addDays(1);
    m_source.events = {declined, hidden, holiday};
    m_scheduler->setHiddenCalendars({u"other"_s});
    m_scheduler->setEnabled(true);
    stepTo(at(23, 55));
    QVERIFY(m_said.isEmpty());
}

void TestReminderScheduler::allDayRemindersFollowTheZone()
{
    // 15 hours before the day: 9:00 the day before, in Berlin.
    Event birthday =
        eventAt(u"birthday"_s, QDateTime(QDate(2026, 10, 8), QTime(0, 0), QTimeZone::UTC));
    birthday.allDay = true;
    birthday.end = birthday.start.addDays(1);
    birthday.remindersKnown = true;
    birthday.reminders = {15 * 60};
    m_source.events = {birthday};
    const QTimeZone berlin("Europe/Berlin");
    m_scheduler->setTimeZone(berlin);
    m_now = QDateTime(QDate(2026, 10, 7), QTime(8, 0), berlin);
    m_scheduler->setEnabled(true);
    stepTo(QDateTime(QDate(2026, 10, 7), QTime(8, 59), berlin));
    QVERIFY(m_said.isEmpty());
    stepTo(QDateTime(QDate(2026, 10, 7), QTime(9, 0), berlin));
    QCOMPARE(m_said, (QList<QPair<QString, int>>{{u"birthday"_s, 15 * 60}}));
}

void TestReminderScheduler::recentReminderIsSaidOnStart()
{
    m_source.events = {eventAt(u"soon"_s, at(9, 8)), eventAt(u"missed"_s, at(9, 3))};
    m_scheduler->setEnabled(true);
    // "soon" fell due two minutes ago; "missed" seven.
    QCOMPARE(m_said, (QList<QPair<QString, int>>{{u"soon"_s, 10}}));
}

void TestReminderScheduler::endedEventsAreNotCaughtUp()
{
    m_source.events = {eventAt(u"morning"_s, at(10)), eventAt(u"noon"_s, at(12))};
    m_scheduler->setEnabled(true);
    // As if the computer slept from 9:00 until 11:55.
    stepTo(at(11, 55));
    QCOMPARE(m_said, (QList<QPair<QString, int>>{{u"noon"_s, 10}}));
}

void TestReminderScheduler::snoozeRemindsAgain()
{
    const Event call = eventAt(u"call"_s, at(10));
    m_scheduler->setEnabled(true);
    m_scheduler->snooze(call, 5);
    stepTo(at(9, 4));
    QVERIFY(m_said.isEmpty());
    stepTo(at(9, 5));
    QCOMPARE(m_said, (QList<QPair<QString, int>>{{u"call"_s, -1}}));
    stepTo(at(9, 10));
    QCOMPARE(m_said.size(), 1);
}

void TestReminderScheduler::stoppedSaysNothing()
{
    m_source.events = {eventAt(u"standup"_s, at(10))};
    m_scheduler->setEnabled(true);
    m_scheduler->setEnabled(false);
    stepTo(at(9, 50));
    QVERIFY(m_said.isEmpty());

    // Turning back on later does not catch up on what was skipped.
    m_now = at(9, 58);
    m_scheduler->setDefaultMinutes(15);
    m_scheduler->setEnabled(true);
    QVERIFY(m_said.isEmpty());
}

QTEST_GUILESS_MAIN(TestReminderScheduler)
#include "tst_reminderscheduler.moc"
