#include "callie/CalendarSource.h"
#include "callie/EventModel.h"

#include <QPromise>
#include <QSignalSpy>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

const QDate kMonday(2026, 3, 16);

/// Returns exactly the events it was given, filtered to the requested window,
/// so lane assignment can be tested without depending on the clock.
class FakeSource : public CalendarSource
{
public:
    explicit FakeSource(QList<Event> events) : m_events(std::move(events)) {}

    QString sourceId() const override { return QStringLiteral("fake"); }
    QList<CalendarInfo> calendars() const override { return calendarList; }
    void refresh() override { Q_EMIT changed(); }

    QList<Event> eventsBetween(const QDateTime &from, const QDateTime &to,
                               const QTimeZone &tz) const override
    {
        QList<Event> out;
        for (Event e : m_events) {
            if (e.end > from && e.start < to) {
                e.start = e.start.toTimeZone(tz);
                e.end = e.end.toTimeZone(tz);
                out.append(e);
            }
        }
        return out;
    }

    QList<CalendarInfo> calendarList;

private:
    QList<Event> m_events;
};

/// Answers each load only when the test says so, like a source reading on
/// another thread.
class SlowSource : public CalendarSource
{
public:
    QString sourceId() const override { return QStringLiteral("slow"); }
    QList<CalendarInfo> calendars() const override { return {}; }
    QList<Event> eventsBetween(const QDateTime &, const QDateTime &,
                               const QTimeZone &) const override
    {
        return {};
    }
    QFuture<SourceSnapshot> load(const QDateTime &, const QDateTime &,
                                 const QTimeZone &) const override
    {
        auto promise = std::make_shared<QPromise<SourceSnapshot>>();
        promise->start();
        loads.append(promise);
        return promise->future();
    }

    void answer(int load, const QList<Event> &events)
    {
        loads.at(load)->addResult(SourceSnapshot{events, {}});
        loads.at(load)->finish();
    }

    void refresh() override {}
    void reloadSame() { Q_EMIT changed(); }

    mutable QList<std::shared_ptr<QPromise<SourceSnapshot>>> loads;
};

Event timed(const QString &uid, const QDate &date, int startHour, int startMinute,
            int durationMinutes)
{
    Event e;
    e.uid = uid;
    e.summary = uid;
    e.start = QDateTime(date, QTime(startHour, startMinute), QTimeZone::systemTimeZone());
    e.end = e.start.addSecs(durationMinutes * 60);
    return e;
}

Event allDay(const QString &uid, const QDate &first, int days)
{
    Event e;
    e.uid = uid;
    e.summary = uid;
    e.allDay = true;
    e.start = QDateTime(first, QTime(0, 0), QTimeZone::systemTimeZone());
    e.end = QDateTime(first.addDays(days), QTime(0, 0), QTimeZone::systemTimeZone());
    return e;
}

} // namespace

class TestEventModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void singleEventGetsOneLane();
    void separatedEventsShareLaneZero();
    void backToBackEventsDoNotOverlap();
    void twoOverlappingEventsSplitLanes();
    void threeWayOverlapUsesThreeLanes();
    void clustersAreCountedIndependently();
    void differentDaysDoNotShareLanes();
    void positionRolesAreComputed();
    void changingRangeReloads();
    void invalidRangeStartIsIgnored();
    void allDayEventsStackInRows();
    void allDayEventIsClippedToRange();
    void allDayEndOnDayWithoutMidnight();
    void declinedIsARole();
    void calendarsListShownOnly();
    void daysOffFollowTheLocale();
    void rowsNameTheirCalendar();
    void timingDescribesWhereAnEventStands();
    void callServiceTrustsOnlyTheHost();
    void slowLoadKeepsRowsForTheSameRange();
    void overtakenLoadIsDropped();
    void hiddenCalendarsAndDeclinedAreLeftOut();
    void timeZoneMovesEvents();

private:
    /// Builds a model over `events` starting at kMonday. Rows keep source order.
    static std::pair<std::unique_ptr<EventModel>, std::unique_ptr<FakeSource>>
    modelFor(const QList<Event> &events, int dayCount = 7)
    {
        auto source = std::make_unique<FakeSource>(events);
        auto model = std::make_unique<EventModel>();
        model->setDayCount(dayCount);
        model->setRangeStart(kMonday);
        model->setSource(source.get());
        return {std::move(model), std::move(source)};
    }

    static int intRole(const EventModel &m, int row, EventModel::Role role)
    {
        return m.data(m.index(row, 0), role).toInt();
    }
};

void TestEventModel::singleEventGetsOneLane()
{
    auto [model, source] = modelFor({timed("a", kMonday, 9, 0, 60)});

    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(intRole(*model, 0, EventModel::LaneRole), 0);
    QCOMPARE(intRole(*model, 0, EventModel::LaneCountRole), 1);
}

void TestEventModel::separatedEventsShareLaneZero()
{
    auto [model, source] = modelFor({
        timed("a", kMonday, 9, 0, 30),
        timed("b", kMonday, 10, 0, 30),
        timed("c", kMonday, 11, 0, 30),
    });

    QCOMPARE(model->rowCount(), 3);
    for (int row = 0; row < 3; ++row) {
        QCOMPARE(intRole(*model, row, EventModel::LaneRole), 0);
        QCOMPARE(intRole(*model, row, EventModel::LaneCountRole), 1);
    }
}

void TestEventModel::backToBackEventsDoNotOverlap()
{
    // An event ending exactly when the next begins must not consume a lane.
    auto [model, source] = modelFor({
        timed("a", kMonday, 9, 0, 60),
        timed("b", kMonday, 10, 0, 60),
    });

    QCOMPARE(intRole(*model, 0, EventModel::LaneRole), 0);
    QCOMPARE(intRole(*model, 1, EventModel::LaneRole), 0);
    QCOMPARE(intRole(*model, 0, EventModel::LaneCountRole), 1);
    QCOMPARE(intRole(*model, 1, EventModel::LaneCountRole), 1);
}

void TestEventModel::twoOverlappingEventsSplitLanes()
{
    auto [model, source] = modelFor({
        timed("a", kMonday, 9, 0, 60),
        timed("b", kMonday, 9, 30, 60),
    });

    QCOMPARE(intRole(*model, 0, EventModel::LaneRole), 0);
    QCOMPARE(intRole(*model, 1, EventModel::LaneRole), 1);
    QCOMPARE(intRole(*model, 0, EventModel::LaneCountRole), 2);
    QCOMPARE(intRole(*model, 1, EventModel::LaneCountRole), 2);
}

void TestEventModel::threeWayOverlapUsesThreeLanes()
{
    auto [model, source] = modelFor({
        timed("wide", kMonday, 9, 0, 120),
        timed("mid", kMonday, 9, 30, 60),
        timed("short", kMonday, 10, 0, 15),
    });

    QCOMPARE(intRole(*model, 0, EventModel::LaneRole), 0);
    QCOMPARE(intRole(*model, 1, EventModel::LaneRole), 1);
    QCOMPARE(intRole(*model, 2, EventModel::LaneRole), 2);
    for (int row = 0; row < 3; ++row)
        QCOMPARE(intRole(*model, row, EventModel::LaneCountRole), 3);
}

void TestEventModel::clustersAreCountedIndependently()
{
    // A gap ends the cluster, so the later event must not inherit laneCount 2.
    auto [model, source] = modelFor({
        timed("a", kMonday, 9, 0, 60),
        timed("b", kMonday, 9, 30, 60),
        timed("afternoon", kMonday, 14, 0, 60),
    });

    QCOMPARE(intRole(*model, 0, EventModel::LaneCountRole), 2);
    QCOMPARE(intRole(*model, 1, EventModel::LaneCountRole), 2);
    QCOMPARE(intRole(*model, 2, EventModel::LaneRole), 0);
    QCOMPARE(intRole(*model, 2, EventModel::LaneCountRole), 1);
}

void TestEventModel::differentDaysDoNotShareLanes()
{
    // Same wall-clock time on two days must not be treated as an overlap.
    auto [model, source] = modelFor({
        timed("mon", kMonday, 9, 0, 60),
        timed("tue", kMonday.addDays(1), 9, 0, 60),
    });

    for (int row = 0; row < 2; ++row) {
        QCOMPARE(intRole(*model, row, EventModel::LaneRole), 0);
        QCOMPARE(intRole(*model, row, EventModel::LaneCountRole), 1);
    }
    QCOMPARE(intRole(*model, 0, EventModel::DayIndexRole), 0);
    QCOMPARE(intRole(*model, 1, EventModel::DayIndexRole), 1);
}

void TestEventModel::positionRolesAreComputed()
{
    auto [model, source] = modelFor({timed("a", kMonday.addDays(2), 9, 30, 45)});

    QCOMPARE(intRole(*model, 0, EventModel::DayIndexRole), 2);
    QCOMPARE(intRole(*model, 0, EventModel::StartMinutesRole), 9 * 60 + 30);
    QCOMPARE(intRole(*model, 0, EventModel::DurationMinutesRole), 45);
}

void TestEventModel::changingRangeReloads()
{
    auto [model, source] = modelFor({
        timed("thisWeek", kMonday, 9, 0, 60),
        timed("nextWeek", kMonday.addDays(9), 9, 0, 60),
    });

    QCOMPARE(model->rowCount(), 1);

    QSignalSpy reset(model.get(), &QAbstractItemModel::modelReset);
    model->setRangeStart(kMonday.addDays(7));

    QCOMPARE(reset.count(), 1);
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(model->data(model->index(0, 0), EventModel::UidRole).toString(),
             QStringLiteral("nextWeek"));
}

void TestEventModel::invalidRangeStartIsIgnored()
{
    auto [model, source] = modelFor({timed("a", kMonday, 9, 0, 60)});

    model->setRangeStart(QDate());

    QCOMPARE(model->rangeStart(), kMonday);
    QCOMPARE(model->rowCount(), 1);
}

void TestEventModel::allDayEventsStackInRows()
{
    auto [model, source] = modelFor({
        allDay("trip", kMonday, 3),
        allDay("tuesday", kMonday.addDays(1), 1),
        allDay("thursday", kMonday.addDays(3), 1),
    });

    QCOMPARE(model->allDayRows(), 2);
    QCOMPARE(intRole(*model, 0, EventModel::FirstDayRole), 0);
    QCOMPARE(intRole(*model, 0, EventModel::DaySpanRole), 3);
    QCOMPARE(intRole(*model, 0, EventModel::LaneRole), 0);
    QCOMPARE(intRole(*model, 1, EventModel::LaneRole), 1);
    QCOMPARE(intRole(*model, 2, EventModel::LaneRole), 0);
}

void TestEventModel::allDayEventIsClippedToRange()
{
    auto [model, source] = modelFor({allDay("long", kMonday.addDays(-5), 7)});

    QCOMPARE(intRole(*model, 0, EventModel::FirstDayRole), 0);
    QCOMPARE(intRole(*model, 0, EventModel::DaySpanRole), 2);
    QCOMPARE(model->allDayRows(), 1);
}

void TestEventModel::allDayEndOnDayWithoutMidnight()
{
    // Chile springs forward at midnight on 2026-09-06, so that midnight is 01:00.
    const QTimeZone santiago("America/Santiago");
    Event e;
    e.uid = QStringLiteral("saturday");
    e.allDay = true;
    e.start = QDateTime(QDate(2026, 9, 5), QTime(0, 0), santiago);
    e.end = QDateTime(QDate(2026, 9, 6), QTime(0, 0), santiago);
    QCOMPARE(e.end.time(), QTime(1, 0));

    FakeSource source({e});
    EventModel model;
    model.setRangeStart(QDate(2026, 9, 1));
    model.setSource(&source);

    QCOMPARE(intRole(model, 0, EventModel::FirstDayRole), 4);
    QCOMPARE(intRole(model, 0, EventModel::DaySpanRole), 1);
}

void TestEventModel::declinedIsARole()
{
    Event skipped = timed("skipped", kMonday, 9, 0, 60);
    skipped.declined = true;
    auto [model, source] = modelFor({skipped, timed("going", kMonday, 11, 0, 60)});

    QVERIFY(model->roleNames().value(EventModel::DeclinedRole) == "declined");
    QVERIFY(model->data(model->index(0, 0), EventModel::DeclinedRole).toBool());
    QVERIFY(!model->data(model->index(1, 0), EventModel::DeclinedRole).toBool());
}

void TestEventModel::calendarsListShownOnly()
{
    auto source = std::make_unique<FakeSource>(QList<Event>{});
    source->calendarList = {
        CalendarInfo{.id = "a", .displayName = "Shown", .color = QColor("#ff0000")},
        CalendarInfo{
            .id = "b", .displayName = "Hidden", .color = {}, .writable = false, .enabled = false},
    };
    EventModel model;
    QSignalSpy changed(&model, &EventModel::calendarsChanged);
    model.setSource(source.get());

    QCOMPARE(changed.size(), 1);
    QCOMPARE(model.calendars().size(), 1);
    const QVariantMap shown = model.calendars().first().toMap();
    QCOMPARE(shown.value("name").toString(), QStringLiteral("Shown"));
    QCOMPARE(shown.value("color").value<QColor>(), QColor("#ff0000"));
}

void TestEventModel::daysOffFollowTheLocale()
{
    EventModel model;
    const QLocale previous;

    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    QVERIFY(model.isDayOff(QDate(2026, 3, 21)));  // Saturday
    QVERIFY(model.isDayOff(QDate(2026, 3, 22)));  // Sunday
    QVERIFY(!model.isDayOff(QDate(2026, 3, 20))); // Friday

    // Where the weekend is Friday and Saturday.
    QLocale::setDefault(QLocale(QLocale::Arabic, QLocale::SaudiArabia));
    QVERIFY(model.isDayOff(QDate(2026, 3, 20)));
    QVERIFY(!model.isDayOff(QDate(2026, 3, 22)));

    QLocale::setDefault(previous);
}

void TestEventModel::rowsNameTheirCalendar()
{
    Event e = timed("a", kMonday, 9, 0, 60);
    e.calendarId = QStringLiteral("work");
    e.description =
        QStringLiteral("<b>Agenda</b> in the <a href=\"https://x.test\">doc</a><br><img "
                       "src=\"https://x.test/i.png\">");
    auto source = std::make_unique<FakeSource>(QList<Event>{e});
    source->calendarList = {CalendarInfo{
        .id = QStringLiteral("work"), .displayName = QStringLiteral("Work"), .color = QColor()}};
    EventModel model;
    model.setRangeStart(kMonday);
    model.setSource(source.get());

    QCOMPARE(model.data(model.index(0, 0), EventModel::CalendarNameRole).toString(),
             QStringLiteral("Work"));
    QCOMPARE(model.data(model.index(0, 0), EventModel::DescriptionRole).toString(),
             QStringLiteral("Agenda in the doc"));

    e.description = QStringLiteral("R&amp;D sync\nRooms 2 & 3");
    FakeSource escaped({e});
    model.setSource(&escaped);
    QCOMPARE(model.data(model.index(0, 0), EventModel::DescriptionRole).toString(),
             QStringLiteral("R&D sync\nRooms 2 & 3"));
}

void TestEventModel::timingDescribesWhereAnEventStands()
{
    EventModel model;
    const QTimeZone zone = QTimeZone::systemTimeZone();
    const QDateTime now(kMonday, QTime(10, 40), zone);
    const auto at = [&](int hour, int minute, int day = 0) {
        return QDateTime(kMonday.addDays(day), QTime(hour, minute), zone);
    };

    QCOMPARE(model.timing(at(10, 30), at(11, 30), now),
             QStringLiteral("Happening now, 50 min left"));
    QCOMPARE(model.timing(at(9, 0), at(12, 40), now), QStringLiteral("Happening now, 2 h left"));
    QCOMPARE(model.timing(at(11, 0), at(11, 30), now), QStringLiteral("Starts in 20 min"));
    QCOMPARE(model.timing(at(16, 0), at(17, 0), now), QStringLiteral("Starts at 16:00"));
    QCOMPARE(model.timing(at(8, 0), at(9, 0), now), QStringLiteral("Ended"));
    QCOMPARE(model.timing(at(9, 0, 1), at(10, 0, 1), now), QString());
    QCOMPARE(model.timing(at(10, 0), at(12, 15), now),
             QStringLiteral("Happening now, 1 h 35 min left"));
}

void TestEventModel::callServiceTrustsOnlyTheHost()
{
    EventModel model;
    QCOMPARE(model.callService(QUrl(u"https://acme.zoom.us/j/123"_s)), u"zoom"_s);
    QCOMPARE(model.callService(QUrl(u"https://meet.google.com/abc-defg-hij"_s)), u"meet"_s);
    QCOMPARE(model.callService(QUrl(u"https://evil.example/?r=meet.google.com"_s)), u"web"_s);
    QCOMPARE(model.callService(QUrl(u"https://meet.google.com.evil.example/"_s)), u"web"_s);
    QCOMPARE(model.callService(QUrl(u"https://notzoom.us/j/1"_s)), u"web"_s);
    QVERIFY(model.callService(QUrl(u"file:///etc/passwd"_s)).isEmpty());
    QVERIFY(model.callService(QUrl(u"javascript:alert(1)"_s)).isEmpty());
    QVERIFY(model.callService(QUrl()).isEmpty());
}

void TestEventModel::slowLoadKeepsRowsForTheSameRange()
{
    SlowSource source;
    EventModel model;
    model.setRangeStart(kMonday);
    model.setSource(&source);
    source.answer(0, {timed("a", kMonday, 9, 0, 60)});
    QTRY_COMPARE(model.rowCount(), 1);

    // A sync re-reads the same range: the rows stay until the answer.
    source.reloadSame();
    QCoreApplication::processEvents();
    QCOMPARE(model.rowCount(), 1);
    source.answer(1, {timed("a", kMonday, 9, 0, 60), timed("b", kMonday, 11, 0, 60)});
    QTRY_COMPARE(model.rowCount(), 2);

    // Another week empties at once, since the old rows belong to the old days.
    model.setRangeStart(kMonday.addDays(7));
    QCOMPARE(model.rowCount(), 0);
    source.answer(2, {timed("c", kMonday.addDays(7), 9, 0, 60)});
    QTRY_COMPARE(model.rowCount(), 1);
}

void TestEventModel::overtakenLoadIsDropped()
{
    SlowSource source;
    EventModel model;
    model.setRangeStart(kMonday);
    model.setSource(&source);
    model.setRangeStart(kMonday.addDays(7));

    source.answer(1, {timed("b", kMonday.addDays(7), 9, 0, 60)});
    QTRY_COMPARE(model.rowCount(), 1);
    source.answer(0, {timed("a", kMonday, 9, 0, 60), timed("c", kMonday, 11, 0, 60)});
    QTest::qWait(50);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), EventModel::SummaryRole).toString(), u"b");
}

void TestEventModel::hiddenCalendarsAndDeclinedAreLeftOut()
{
    Event work = timed("work", kMonday, 9, 0, 60);
    work.calendarId = QStringLiteral("work");
    Event home = timed("home", kMonday, 11, 0, 60);
    home.calendarId = QStringLiteral("home");
    Event skipped = timed("skipped", kMonday, 13, 0, 60);
    skipped.calendarId = QStringLiteral("home");
    skipped.declined = true;
    auto [model, source] = modelFor({work, home, skipped});
    QCOMPARE(model->rowCount(), 3);

    model->setShowDeclined(false);
    QCOMPARE(model->rowCount(), 2);
    model->setHiddenCalendars({QStringLiteral("work")});
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(model->data(model->index(0, 0), EventModel::UidRole).toString(), u"home");
    model->setShowDeclined(true);
    model->setHiddenCalendars({});
    QCOMPARE(model->rowCount(), 3);
}

void TestEventModel::timeZoneMovesEvents()
{
    // 23:30 UTC on Monday is already Tuesday in Tokyo.
    Event late;
    late.uid = QStringLiteral("late");
    late.start = QDateTime(kMonday, QTime(23, 30), QTimeZone::UTC);
    late.end = late.start.addSecs(1800);
    auto [model, source] = modelFor({late});
    model->setTimeZone(QTimeZone::UTC);
    QCOMPARE(model->data(model->index(0, 0), EventModel::DayIndexRole).toInt(), 0);

    model->setTimeZone(QTimeZone("Asia/Tokyo"));
    QCOMPARE(model->data(model->index(0, 0), EventModel::DayIndexRole).toInt(), 1);
}

QTEST_GUILESS_MAIN(TestEventModel)
#include "tst_eventmodel.moc"
