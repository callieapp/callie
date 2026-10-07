#include "Clock.h"
#include "EventModelForeign.h"
#include "ThemeController.h"

#include "callie/SampleSource.h"
#include "callie/Settings.h"

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

#include <functional>

Q_IMPORT_QML_PLUGIN(Callie_UiPlugin)

using namespace callie;
using namespace Qt::StringLiterals;

class TestWeekDrag : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void dragMovesStretchesAndClickStillOpens();
    void allDayAndMonthDragsMoveByDays();
};

/// The visible event block showing `summary`.
static QQuickItem *findBlock(QQuickItem *item, const QString &summary)
{
    if (QString::fromLatin1(item->metaObject()->className()).startsWith(u"EventBlock") &&
        item->property("summary").toString() == summary && item->isVisible())
        return item;
    for (QQuickItem *child : item->childItems())
        if (QQuickItem *found = findBlock(child, summary))
            return found;
    return nullptr;
}

/// The first visible item whose class starts with `type` and that `match` accepts.
static QQuickItem *findItem(QQuickItem *item, QLatin1StringView type,
                            const std::function<bool(QQuickItem *)> &match)
{
    if (QString::fromLatin1(item->metaObject()->className()).startsWith(type) &&
        item->isVisible() && match(item))
        return item;
    for (QQuickItem *child : item->childItems())
        if (QQuickItem *found = findItem(child, type, match))
            return found;
    return nullptr;
}

static Event named(SampleSource &source, const QString &summary)
{
    const QDateTime from(QDate(2026, 9, 28), QTime(0, 0));
    for (const Event &e : source.eventsBetween(from, from.addDays(42), QTimeZone::systemTimeZone()))
        if (e.summary == summary)
            return e;
    return {};
}

/// Friday's climbing session in the sample week, wherever it has moved to.
static Event climbing(SampleSource &source)
{
    const QDateTime from(QDate(2026, 10, 5), QTime(0, 0));
    for (const Event &e : source.eventsBetween(from, from.addDays(7), QTimeZone::systemTimeZone()))
        if (e.summary == u"Climbing")
            return e;
    return {};
}

/// Presses at `from` and moves to `to` in steps, as a hand would.
static void drag(QQuickWindow *window, QPoint from, QPoint to)
{
    QTest::mousePress(window, Qt::LeftButton, {}, from);
    for (int i = 1; i <= 10; ++i) {
        QTest::mouseMove(window, from + (to - from) * i / 10);
        QTest::qWait(16);
    }
    QTest::mouseRelease(window, Qt::LeftButton, {}, to);
    QTest::qWait(300);
}

void TestWeekDrag::dragMovesStretchesAndClickStillOpens()
{
    QQuickStyle::setStyle(u"Basic"_s);
    QTemporaryDir dir;
    Settings settings(dir.filePath(u"s.ini"_s));
    SettingsForeign::s_instance = &settings;
    Clock::instance()->freeze(QDateTime(QDate(2026, 10, 7), QTime(13, 40)));
    SampleSource source;
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{u"source"_s, QVariant::fromValue<CalendarSource *>(&source)}});
    engine.loadFromModule("Callie.Ui", "Main");
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    // Tall enough that the evening's events and their bottom edges are on screen.
    window->resize(1280, 1300);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTest::qWait(500);

    const Event before = climbing(source);
    QQuickItem *block = nullptr;
    QTRY_VERIFY((block = findBlock(window->contentItem(), u"Climbing"_s)));
    const QPoint center = block->mapToScene(QPointF(block->width() / 2, 10)).toPoint();

    // One hour later and one day earlier.
    const int hour = ThemeController::instance()->hourHeight();
    drag(window, center, center + QPoint(-int(block->width()) - 10, hour));
    const Event moved = climbing(source);
    QCOMPARE(moved.start, before.start.addDays(-1).addSecs(3600));
    QCOMPARE(moved.end, before.end.addDays(-1).addSecs(3600));

    // Stretching the bottom edge half an hour keeps the start.
    QTRY_VERIFY((block = findBlock(window->contentItem(), u"Climbing"_s)));
    QTest::qWait(300);
    const QPoint bottom =
        block->mapToScene(QPointF(block->width() / 2, block->height() - 2)).toPoint();
    drag(window, bottom, bottom + QPoint(0, hour / 2));
    const Event stretched = climbing(source);
    QCOMPARE(stretched.start, moved.start);
    QCOMPARE(stretched.end, moved.end.addSecs(30 * 60));

    // A plain click still opens the details, and moves nothing.
    QTRY_VERIFY((block = findBlock(window->contentItem(), u"Climbing"_s)));
    QTest::qWait(300);
    QTest::mouseClick(window, Qt::LeftButton, {},
                      block->mapToScene(QPointF(block->width() / 2, 10)).toPoint());
    const auto detailsOpen = [&engine] {
        for (QObject *o : engine.rootObjects().first()->findChildren<QObject *>())
            if (QString::fromLatin1(o->metaObject()->className()).startsWith(u"EventDetails") &&
                o->property("opened").toBool())
                return true;
        return false;
    };
    // Waited for, since a loaded machine can take a while to open it.
    QTRY_VERIFY_WITH_TIMEOUT(detailsOpen(), 5000);
    const Event after = climbing(source);
    QCOMPARE(after.start, stretched.start);

    // With weekends hidden, a drag past the last column lands on the last day
    // shown, not on a hidden one.
    QTest::keyClick(window, Qt::Key_Escape);
    settings.setHideWeekends(true);
    QTRY_VERIFY((block = findBlock(window->contentItem(), u"Climbing"_s)));
    QTest::qWait(300);
    const QPoint grip = block->mapToScene(QPointF(block->width() / 2, 10)).toPoint();
    drag(window, grip, QPoint(window->width() - 4, grip.y()));
    QCOMPARE(climbing(source).start.date(), QDate(2026, 10, 9));
}

void TestWeekDrag::allDayAndMonthDragsMoveByDays()
{
    QQuickStyle::setStyle(u"Basic"_s);
    QTemporaryDir dir;
    Settings settings(dir.filePath(u"s.ini"_s));
    SettingsForeign::s_instance = &settings;
    Clock::instance()->freeze(QDateTime(QDate(2026, 10, 7), QTime(13, 40)));
    SampleSource source;
    EventDraft trip;
    trip.calendarId = source.calendars().first().id;
    trip.summary = u"Trip"_s;
    trip.allDay = true;
    trip.start = QDate(2026, 10, 6).startOfDay();
    trip.end = QDate(2026, 10, 8).startOfDay();
    source.createEvent(trip, [](const QString &) {});
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{u"source"_s, QVariant::fromValue<CalendarSource *>(&source)}});
    engine.loadFromModule("Callie.Ui", "Main");
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    window->resize(1280, 1000);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTest::qWait(500);

    const auto chip = [window] {
        return findItem(window->contentItem(), "AllDayChip"_L1,
                        [](QQuickItem *i) { return i->property("summary").toString() == u"Trip"; });
    };
    QQuickItem *item = nullptr;
    QTRY_VERIFY((item = chip()));
    const int day = int(item->width() / 2);

    // A day later, by the middle.
    QPoint grip = item->mapToScene(QPointF(10, item->height() / 2)).toPoint();
    drag(window, grip, grip + QPoint(day + 6, 0));
    QCOMPARE(named(source, u"Trip"_s).start.date(), QDate(2026, 10, 7));
    QCOMPARE(named(source, u"Trip"_s).end.date(), QDate(2026, 10, 9));

    // A day longer, by the right end.
    QTRY_VERIFY((item = chip()));
    QTest::qWait(300);
    grip = item->mapToScene(QPointF(item->width() - 3, item->height() / 2)).toPoint();
    drag(window, grip, grip + QPoint(day, 0));
    QCOMPARE(named(source, u"Trip"_s).start.date(), QDate(2026, 10, 7));
    QCOMPARE(named(source, u"Trip"_s).end.date(), QDate(2026, 10, 10));

    // In the month, a timed event dragged a row down is a week later, same time.
    const Event before = named(source, u"Climbing"_s);
    settings.setView(u"month"_s);
    QQuickItem *month = nullptr;
    QTRY_VERIFY((month = findItem(window->contentItem(), "MonthView"_L1,
                                  [](QQuickItem *) { return true; })));
    const auto climbing = [window] {
        return findItem(window->contentItem(), "MonthChip"_L1, [](QQuickItem *i) {
            return i->property("event").toMap().value(u"summary"_s).toString() == u"Climbing";
        });
    };
    QTRY_VERIFY((item = climbing()));
    QTest::qWait(300);
    grip = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    drag(window, grip, grip + QPoint(0, int(month->property("cellHeight").toReal())));
    const Event after = named(source, u"Climbing"_s);
    QCOMPARE(after.start, before.start.addDays(7));
    QCOMPARE(after.end, before.end.addDays(7));
}

QTEST_MAIN(TestWeekDrag)
#include "tst_weekdrag.moc"
