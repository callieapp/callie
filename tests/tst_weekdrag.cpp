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

Q_IMPORT_QML_PLUGIN(Callie_UiPlugin)

using namespace callie;
using namespace Qt::StringLiterals;

class TestWeekDrag : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void dragMovesStretchesAndClickStillOpens();
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
}

QTEST_MAIN(TestWeekDrag)
#include "tst_weekdrag.moc"
