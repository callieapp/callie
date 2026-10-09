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
    void closeStartsSitSideBySide();
    void laterStartsStepIn();
    void stepsAndSidesMix();
    void clustersAreCountedIndependently();
    void differentDaysDoNotShareLanes();
    void positionRolesAreComputed();
    void changingRangeReloads();
    void invalidRangeStartIsIgnored();
    void allDayEventsStackInRows();
    void allDayEventIsClippedToRange();
    void allDayEndOnDayWithoutMidnight();
    void declinedIsARole();
    void calendarsListEveryCalendar();
    void daysOffFollowTheLocale();
    void rowsNameTheirCalendar();
    void timingDescribesWhereAnEventStands();
    void eventsOnListsADay();
    void eventAtCarriesWhatActionsNeed();
    void shortEventMakesRoomForTheNext();
    void eventsOnRespectsMidnight();
    void eventsOnSurvivesADstGap();
    void revisionMarksEachReset();
    void timingFollowsZoneAndClock();
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

void TestEventModel::closeStartsSitSideBySide()
{
    auto [model, source] = modelFor({
        timed("a", kMonday, 9, 0, 60),
        timed("b", kMonday, 9, 15, 60),
    });

    QCOMPARE(intRole(*model, 0, EventModel::LaneRole), 0);
    QCOMPARE(intRole(*model, 1, EventModel::LaneRole), 1);
    QCOMPARE(intRole(*model, 0, EventModel::LaneCountRole), 2);
    QCOMPARE(intRole(*model, 1, EventModel::LaneCountRole), 2);
    QCOMPARE(intRole(*model, 0, EventModel::DepthRole), 0);
    QCOMPARE(intRole(*model, 1, EventModel::DepthRole), 0);
}

void TestEventModel::laterStartsStepIn()
{
    auto [model, source] = modelFor({
        timed("wide", kMonday, 9, 0, 120),
        timed("mid", kMonday, 9, 30, 60),
        timed("short", kMonday, 10, 0, 15),
    });

    for (int row = 0; row < 3; ++row) {
        QCOMPARE(intRole(*model, row, EventModel::DepthRole), row);
        QCOMPARE(intRole(*model, row, EventModel::LaneRole), 0);
        QCOMPARE(intRole(*model, row, EventModel::LaneCountRole), 1);
    }
}

void TestEventModel::stepsAndSidesMix()
{
    // Two side by side under a long event, then one that only covers the first.
    auto [model, source] = modelFor({
        timed("workshop", kMonday, 10, 0, 90),
        timed("call", kMonday, 10, 30, 60),
        timed("sync", kMonday, 10, 45, 30),
        timed("late", kMonday, 11, 20, 30),
    });

    QCOMPARE(intRole(*model, 0, EventModel::DepthRole), 0);
    QCOMPARE(intRole(*model, 1, EventModel::DepthRole), 1);
    QCOMPARE(intRole(*model, 2, EventModel::DepthRole), 1);
    QCOMPARE(intRole(*model, 1, EventModel::LaneRole), 0);
    QCOMPARE(intRole(*model, 2, EventModel::LaneRole), 1);
    QCOMPARE(intRole(*model, 2, EventModel::LaneCountRole), 2);
    // "late" overlaps the workshop and the call, not the sync, which has ended.
    QCOMPARE(intRole(*model, 3, EventModel::DepthRole), 2);
    QCOMPARE(intRole(*model, 3, EventModel::LaneCountRole), 1);
}

void TestEventModel::clustersAreCountedIndependently()
{
    // A gap ends the overlap, so the later event starts over at the edge.
    auto [model, source] = modelFor({
        timed("a", kMonday, 9, 0, 60),
        timed("b", kMonday, 9, 10, 60),
        timed("afternoon", kMonday, 14, 0, 60),
    });

    QCOMPARE(intRole(*model, 0, EventModel::LaneCountRole), 2);
    QCOMPARE(intRole(*model, 1, EventModel::LaneCountRole), 2);
    QCOMPARE(intRole(*model, 2, EventModel::LaneRole), 0);
    QCOMPARE(intRole(*model, 2, EventModel::LaneCountRole), 1);
    QCOMPARE(intRole(*model, 2, EventModel::DepthRole), 0);
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

void TestEventModel::calendarsListEveryCalendar()
{
    auto source = std::make_unique<FakeSource>(QList<Event>{});
    source->calendarList = {
        CalendarInfo{.id = "a",
                     .displayName = "Shown",
                     .color = QColor("#ff0000"),
                     .account = QStringLiteral("me@example.com")},
        CalendarInfo{
            .id = "b", .displayName = "Hidden", .color = {}, .writable = false, .enabled = false},
    };
    EventModel model;
    QSignalSpy changed(&model, &EventModel::calendarsChanged);
    model.setSource(source.get());

    // One the provider hides is listed too; whether it shows is Callie's to say.
    QCOMPARE(changed.size(), 1);
    QCOMPARE(model.calendars().size(), 2);
    const QVariantMap shown = model.calendars().first().toMap();
    QCOMPARE(shown.value("name").toString(), QStringLiteral("Shown"));
    QCOMPARE(shown.value("color").value<QColor>(), QColor("#ff0000"));
    QCOMPARE(shown.value("id").toString(), QStringLiteral("a"));
    QCOMPARE(shown.value("account").toString(), QStringLiteral("me@example.com"));
    QCOMPARE(model.calendars().last().toMap().value("id").toString(), QStringLiteral("b"));
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
    // No call of its own, and none in its place or notes.
    QVERIFY(model.data(model.index(0, 0), EventModel::JoinUrlRole).toUrl().isEmpty());

    e.description = QStringLiteral("R&amp;D sync\nRooms 2 & 3");
    FakeSource escaped({e});
    model.setSource(&escaped);
    QCOMPARE(model.data(model.index(0, 0), EventModel::DescriptionRole).toString(),
             QStringLiteral("R&D sync\nRooms 2 & 3"));

    // A call link in the notes is what Join opens; the event still has no call of its own.
    e.description = QStringLiteral("Join at https://acme.zoom.us/j/123");
    FakeSource linked({e});
    model.setSource(&linked);
    QCOMPARE(model.data(model.index(0, 0), EventModel::JoinUrlRole).toUrl(),
             QUrl(QStringLiteral("https://acme.zoom.us/j/123")));
    QVERIFY(model.data(model.index(0, 0), EventModel::ConferenceUrlRole).toUrl().isEmpty());
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
    QCOMPARE(model.callService(QUrl(u"https://teams.microsoft.com/meet/1"_s)), u"teams"_s);
    QCOMPARE(model.callService(QUrl(u"https://teams.live.com/meet/1"_s)), u"teams"_s);
    QCOMPARE(model.callService(QUrl(u"https://teams.microsoft.com.evil.example/"_s)), u"web"_s);
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

void TestEventModel::timingFollowsZoneAndClock()
{
    EventModel model;
    model.setTimeZoneId(QStringLiteral("Asia/Tokyo"));
    QCOMPARE(model.timeZoneId(), QStringLiteral("Asia/Tokyo"));
    // 10:40 UTC is 19:40 in Tokyo; 13:00 UTC is 22:00 there, the same day.
    const QDateTime now(kMonday, QTime(10, 40), QTimeZone::UTC);
    const QDateTime later(kMonday, QTime(13, 0), QTimeZone::UTC);
    QCOMPARE(model.timing(later, later.addSecs(1800), now), QStringLiteral("Starts at 22:00"));

    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    model.setProperty("use24Hour", false);
    QCOMPARE(model.timing(later, later.addSecs(1800), now), QStringLiteral("Starts at 10:00 PM"));
    QLocale::setDefault(QLocale::c());

    // 16:00 UTC is already tomorrow in Tokyo, where the date says enough.
    const QDateTime tomorrow(kMonday, QTime(16, 0), QTimeZone::UTC);
    QCOMPARE(model.timing(tomorrow, tomorrow.addSecs(1800), now), QString());

    model.setTimeZoneId({});
    QVERIFY(model.timeZoneId().isEmpty());
}

void TestEventModel::eventsOnListsADay()
{
    Event overnight = timed("overnight", kMonday, 22, 0, 4 * 60);
    auto [model, source] =
        modelFor({timed("late", kMonday, 15, 0, 60), timed("early", kMonday, 9, 0, 60),
                  allDay("holiday", kMonday, 2), overnight,
                  timed("tuesday", kMonday.addDays(1), 10, 0, 30)});

    const auto summaries = [&](int day) {
        QStringList list;
        for (const QVariant &event : model->eventsOn(day))
            list << event.toMap().value(QStringLiteral("summary")).toString();
        return list;
    };
    QCOMPARE(summaries(0), (QStringList{"holiday", "early", "late", "overnight"}));
    // The holiday spans two days and the overnight event runs past midnight.
    QCOMPARE(summaries(1), (QStringList{"holiday", "overnight", "tuesday"}));
    QVERIFY(summaries(2).isEmpty());

    const QVariantMap first = model->eventsOn(0).first().toMap();
    QVERIFY(first.value(QStringLiteral("allDay")).toBool());
    QVERIFY(first.contains(QStringLiteral("calendarColor")));
}

void TestEventModel::eventsOnRespectsMidnight()
{
    // Ends exactly at midnight: Monday only. Starts at midnight with no
    // length: Tuesday only.
    Event untilMidnight = timed("evening", kMonday, 22, 0, 120);
    Event instant = timed("instant", kMonday.addDays(1), 0, 0, 0);
    auto [model, source] = modelFor({untilMidnight, instant});

    const auto count = [&](int day, const char *summary) {
        int found = 0;
        for (const QVariant &event : model->eventsOn(day))
            found += event.toMap().value(QStringLiteral("summary")).toString() == summary;
        return found;
    };
    QCOMPARE(count(0, "evening"), 1);
    QCOMPARE(count(1, "evening"), 0);
    QCOMPARE(count(0, "instant"), 0);
    QCOMPARE(count(1, "instant"), 1);
}

void TestEventModel::eventsOnSurvivesADstGap()
{
    // Berlin skips 02:00 to 03:00 on 29 March 2026, a 23-hour Sunday.
    const QTimeZone berlin("Europe/Berlin");
    const QDate saturday(2026, 3, 28);
    Event early;
    early.uid = QStringLiteral("early");
    early.summary = QStringLiteral("early");
    early.start = QDateTime(saturday.addDays(1), QTime(1, 30), berlin);
    early.end = QDateTime(saturday.addDays(1), QTime(3, 30), berlin);
    Event monday = timed("monday", saturday.addDays(2), 0, 30, 30);
    monday.start = QDateTime(saturday.addDays(2), QTime(0, 30), berlin);
    monday.end = monday.start.addSecs(1800);

    FakeSource source({early, monday});
    EventModel model;
    model.setTimeZone(berlin);
    model.setRangeStart(saturday);
    model.setDayCount(3);
    model.setSource(&source);

    QCOMPARE(model.eventsOn(1).size(), 1);
    QCOMPARE(model.eventsOn(1).first().toMap().value(QStringLiteral("summary")).toString(),
             QStringLiteral("early"));
    QCOMPARE(model.eventsOn(2).size(), 1);
}

void TestEventModel::revisionMarksEachReset()
{
    auto [model, source] = modelFor({timed("a", kMonday, 9, 0, 60)});
    QSignalSpy revision(model.get(), &EventModel::revisionChanged);
    const int before = model->revision();

    model->setRangeStart(kMonday.addDays(7));

    QVERIFY(model->revision() > before);
    QCOMPARE(revision.size(), 1);
}

void TestEventModel::shortEventMakesRoomForTheNext()
{
    // Drawn 20 minutes tall, the 15-minute event would cover the next one's top.
    auto [model, source] =
        modelFor({timed("check", kMonday, 11, 45, 15), timed("noon", kMonday, 12, 0, 60)});
    QCOMPARE(intRole(*model, 1, EventModel::LaneCountRole), 1);

    model->setMinimumMinutes(20);
    QCOMPARE(intRole(*model, 0, EventModel::LaneCountRole), 2);
    QCOMPARE(intRole(*model, 1, EventModel::LaneRole), 1);
}

void TestEventModel::eventAtCarriesWhatActionsNeed()
{
    Event occurrence = timed("standup", kMonday, 9, 30, 15);
    occurrence.eventId = QStringLiteral("standup_20260316T093000Z");
    occurrence.seriesId = QStringLiteral("standup");
    occurrence.calendarId = QStringLiteral("work");
    occurrence.recurrenceId = QDateTime(kMonday, QTime(9, 0), QTimeZone::systemTimeZone());
    occurrence.guests = {
        {QStringLiteral("boss@example.com"), {}, QStringLiteral("accepted"), true, false}};
    auto [model, source] = modelFor({occurrence});

    const QVariantMap row = model->eventAt(0);
    QCOMPARE(row.value(QStringLiteral("eventId")).toString(), occurrence.eventId);
    QCOMPARE(row.value(QStringLiteral("seriesId")).toString(), QStringLiteral("standup"));
    QCOMPARE(row.value(QStringLiteral("calendarId")).toString(), QStringLiteral("work"));
    // A moved occurrence is changed by where it was, not where it is.
    QCOMPARE(row.value(QStringLiteral("recurrenceId")).toDateTime(), occurrence.recurrenceId);
    QVERIFY(model->eventAt(1).isEmpty());
    QVERIFY(model->eventAt(-1).isEmpty());
    QCOMPARE(model->rowOf(QStringLiteral("standup"), occurrence.start), 0);
    // A guest without a name goes by their address.
    const QVariantMap guest = row.value(QStringLiteral("guests")).toList().first().toMap();
    QCOMPARE(guest.value(QStringLiteral("name")).toString(), QStringLiteral("boss@example.com"));
    QVERIFY(guest.value(QStringLiteral("organizer")).toBool());
    QCOMPARE(model->rowOf(QStringLiteral("standup"), occurrence.start.addDays(1)), -1);
}

QTEST_GUILESS_MAIN(TestEventModel)
#include "tst_eventmodel.moc"
