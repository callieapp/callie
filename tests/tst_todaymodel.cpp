#include "callie/CalendarSource.h"
#include "callie/TodayModel.h"

#include <QPromise>
#include <QSignalSpy>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

const QDate kDay(2026, 3, 18);

QDateTime at(int hour, int minute = 0, int addDays = 0)
{
    return QDateTime(kDay.addDays(addDays), QTime(hour, minute), QTimeZone::systemTimeZone());
}

Event timed(const QString &summary, QDateTime start, int minutes, const QString &location = {})
{
    Event e;
    e.uid = summary;
    e.summary = summary;
    e.calendarId = u"focus"_s;
    e.color = QColor(u"#7c6bd6"_s);
    e.location = location;
    e.start = start;
    e.end = start.addSecs(minutes * 60);
    return e;
}

class FakeSource : public CalendarSource
{
public:
    explicit FakeSource(QList<Event> events) : m_events(std::move(events)) {}

    QString sourceId() const override { return u"fake"_s; }
    QList<CalendarInfo> calendars() const override
    {
        return {CalendarInfo{.id = u"focus"_s, .displayName = u"Focus"_s, .color = QColor()}};
    }
    void refresh() override { Q_EMIT changed(); }
    QList<Event> eventsBetween(const QDateTime &from, const QDateTime &to,
                               const QTimeZone &) const override
    {
        QList<Event> out;
        for (const Event &e : m_events) {
            if (e.end > from && e.start < to)
                out.append(e);
        }
        return out;
    }

    QList<Event> m_events;
};

/// Answers only when told to, like a source reading on another thread.
class SlowSource : public FakeSource
{
public:
    using FakeSource::FakeSource;

    QFuture<SourceSnapshot> load(const QDateTime &from, const QDateTime &to,
                                 const QTimeZone &tz) const override
    {
        auto promise = std::make_shared<QPromise<SourceSnapshot>>();
        promise->start();
        pending.append({promise, SourceSnapshot{eventsBetween(from, to, tz), calendars()}});
        return promise->future();
    }

    void answer(qsizetype read)
    {
        auto &[promise, snapshot] = pending[read];
        promise->addResult(snapshot);
        promise->finish();
    }

    void answerAll()
    {
        for (auto &[promise, snapshot] : pending) {
            promise->addResult(snapshot);
            promise->finish();
        }
        pending.clear();
    }

    mutable QList<std::pair<std::shared_ptr<QPromise<SourceSnapshot>>, SourceSnapshot>> pending;
};

} // namespace

class TestTodayModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void greetingFollowsTheHour();
    void nextIsTheSoonestStillToStart();
    void soonIsCountedInMinutes();
    void laterNamesTheTime();
    void detailFallsBackToTheCalendar();
    void nothingLeftToday();
    void sourceChangesRefresh();
    void declinedIsSkipped();
    void slowReadLandsLater();
    void startedEventLeavesWhileReading();
    void overtakenReadIsDropped();
    void todayFollowsTheZone();
};

void TestTodayModel::greetingFollowsTheHour()
{
    TodayModel model;
    model.setNow(at(8));
    QCOMPARE(model.greeting(), u"Good morning"_s);
    model.setNow(at(14));
    QCOMPARE(model.greeting(), u"Good afternoon"_s);
    model.setNow(at(21));
    QCOMPARE(model.greeting(), u"Good evening"_s);
    model.setNow(at(2));
    QCOMPARE(model.greeting(), u"Good evening"_s);
}

void TestTodayModel::nextIsTheSoonestStillToStart()
{
    // Already under way, later today, and soonest; all-day never counts.
    Event allDay = timed(u"Birthday"_s, at(0), 24 * 60);
    allDay.allDay = true;
    FakeSource source({timed(u"Workshop"_s, at(10), 90), timed(u"Dentist"_s, at(16), 60),
                       timed(u"Quick sync"_s, at(11), 30), allDay});
    TodayModel model;
    model.setSource(&source);
    model.setNow(at(10, 40));

    QVERIFY(model.hasNext());
    QCOMPARE(model.nextTitle(), u"Quick sync"_s);
}

void TestTodayModel::soonIsCountedInMinutes()
{
    FakeSource source({timed(u"Quick sync"_s, at(11), 30)});
    TodayModel model;
    model.setSource(&source);
    model.setNow(at(10, 40));

    QCOMPARE(model.nextLabel(), u"Up next, in 20 min"_s);
}

void TestTodayModel::laterNamesTheTime()
{
    FakeSource source({timed(u"Dentist"_s, at(16), 60, u"Clinic"_s)});
    TodayModel model;
    model.setSource(&source);
    model.setNow(at(10, 40));

    QCOMPARE(model.nextLabel(), u"Up next at 16:00"_s);
    QCOMPARE(model.nextDetail(), u"16:00 to 17:00, Clinic"_s);

    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    model.setUse24Hour(false);
    QCOMPARE(model.nextLabel(), u"Up next at 4:00 PM"_s);
    QCOMPARE(model.nextDetail(), u"4:00 PM to 5:00 PM, Clinic"_s);
    QLocale::setDefault(QLocale::c());
}

void TestTodayModel::detailFallsBackToTheCalendar()
{
    FakeSource source({timed(u"Quick sync"_s, at(11), 30)});
    TodayModel model;
    model.setSource(&source);
    model.setNow(at(10, 40));

    QCOMPARE(model.nextDetail(), u"11:00 to 11:30, Focus"_s);
}

void TestTodayModel::nothingLeftToday()
{
    FakeSource source({timed(u"Workshop"_s, at(10), 60), timed(u"Tomorrow"_s, at(9, 0, 1), 60)});
    TodayModel model;
    model.setSource(&source);
    model.setNow(at(18));

    QVERIFY(!model.hasNext());
    QCOMPARE(model.nextLabel(), QString());
}

void TestTodayModel::sourceChangesRefresh()
{
    FakeSource source({});
    TodayModel model;
    model.setSource(&source);
    model.setNow(at(10));
    QVERIFY(!model.hasNext());
    QSignalSpy changed(&model, &TodayModel::changed);

    source.m_events = {timed(u"New"_s, at(12), 30)};
    source.refresh();

    QCOMPARE(changed.size(), 1);
    QVERIFY(model.hasNext());
}

void TestTodayModel::declinedIsSkipped()
{
    Event skipped = timed(u"Skipped"_s, at(11), 30);
    skipped.declined = true;
    FakeSource source({skipped, timed(u"Going"_s, at(14), 30)});
    TodayModel model;
    model.setNow(at(10, 40));
    model.setSource(&source);

    QCOMPARE(model.nextTitle(), u"Going"_s);

    model.setHiddenCalendars({u"focus"_s});
    QVERIFY(!model.hasNext());
}

void TestTodayModel::slowReadLandsLater()
{
    SlowSource source({timed(u"Standup"_s, at(11), 15)});
    TodayModel model;
    model.setNow(at(10, 40));
    model.setSource(&source);
    QVERIFY(!model.hasNext());

    source.answerAll();
    QTRY_COMPARE(model.nextTitle(), u"Standup"_s);
}

void TestTodayModel::startedEventLeavesWhileReading()
{
    SlowSource source({timed(u"Standup"_s, at(11), 15), timed(u"Lunch"_s, at(12), 60)});
    TodayModel model;
    model.setNow(at(10, 40));
    model.setSource(&source);
    source.answerAll();
    QTRY_COMPARE(model.nextTitle(), u"Standup"_s);

    model.setNow(at(11, 1));
    QVERIFY(!model.hasNext());
    source.answerAll();
    QTRY_COMPARE(model.nextTitle(), u"Lunch"_s);
}

void TestTodayModel::overtakenReadIsDropped()
{
    SlowSource source({timed(u"Standup"_s, at(11), 15)});
    TodayModel model;
    model.setNow(at(10, 40));
    model.setSource(&source);
    source.m_events = {timed(u"Lunch"_s, at(12), 60)};
    source.refresh();

    // The newer read answers first; the older one, still seeing the standup,
    // must not replace it.
    source.answer(1);
    QTRY_COMPARE(model.nextTitle(), u"Lunch"_s);
    source.answer(0);
    QTest::qWait(50);
    QCOMPARE(model.nextTitle(), u"Lunch"_s);
}

void TestTodayModel::todayFollowsTheZone()
{
    // 22:00 UTC on the 18th is already the 19th in Tokyo.
    TodayModel model;
    model.setNow(QDateTime(kDay, QTime(22, 0), QTimeZone::UTC));
    model.setTimeZone(QTimeZone("Asia/Tokyo"));

    QCOMPARE(model.now().date(), kDay.addDays(1));
    QCOMPARE(model.greeting(), u"Good morning"_s);
}

QTEST_GUILESS_MAIN(TestTodayModel)
#include "tst_todaymodel.moc"
