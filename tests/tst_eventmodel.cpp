#include "callie/CalendarSource.h"
#include "callie/EventModel.h"

#include <QSignalSpy>
#include <QTest>

using namespace callie;

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
                               const QTimeZone &) const override
    {
        QList<Event> out;
        for (const Event &e : m_events) {
            if (e.end > from && e.start < to)
                out.append(e);
        }
        return out;
    }

    QList<CalendarInfo> calendarList;

private:
    QList<Event> m_events;
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
    void allDayEventsStackInRows();
    void allDayEventIsClippedToRange();
    void calendarsListShownOnly();

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

QTEST_GUILESS_MAIN(TestEventModel)
#include "tst_eventmodel.moc"
