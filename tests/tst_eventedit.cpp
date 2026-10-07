#include "Clock.h"
#include "EventModelForeign.h"

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

namespace {

/// The first visible item under `item` whose class starts with `type` and
/// whose `property` reads `value`.
QQuickItem *find(QQuickItem *item, const char *type, const char *property, const QString &value)
{
    if (QString::fromLatin1(item->metaObject()->className()).startsWith(QLatin1StringView(type)) &&
        item->property(property).toString() == value && item->isVisible())
        return item;
    for (QQuickItem *child : item->childItems()) {
        if (QQuickItem *found = find(child, type, property, value))
            return found;
    }
    return nullptr;
}

void click(QQuickWindow *window, QQuickItem *item)
{
    QTest::mouseClick(window, Qt::LeftButton, {},
                      item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
}

Event named(SampleSource &source, const QString &summary)
{
    const QDateTime from(QDate(2026, 10, 5), QTime(0, 0));
    for (const Event &e :
         source.eventsBetween(from, from.addDays(7), QTimeZone::systemTimeZone())) {
        if (e.summary == summary)
            return e;
    }
    return {};
}

} // namespace

class TestEventEdit : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void cardTurnsIntoAFormAndSaves();
};

void TestEventEdit::cardTurnsIntoAFormAndSaves()
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
    window->resize(1280, 1300);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    // Popups live in the overlay, which is under the window's content too.
    QQuickItem *overlay = window->contentItem();

    // Click the climbing session, then Edit on its card.
    QQuickItem *block = nullptr;
    QTRY_VERIFY((block = find(window->contentItem(), "EventBlock", "summary", u"Climbing"_s)));
    QTest::qWait(300);
    click(window, block);
    QQuickItem *edit = nullptr;
    QTRY_VERIFY((edit = find(overlay, "StickerButton", "text", u"Edit"_s)));
    QTest::qWait(300);
    click(window, edit);

    // A new title and place, saved.
    QQuickItem *title = nullptr;
    QTRY_VERIFY((title = find(overlay, "Field", "placeholderText", u"Title"_s)));
    title->setProperty("text", u"Bouldering"_s);
    QMetaObject::invokeMethod(title, "textEdited");
    QQuickItem *where = find(overlay, "Field", "placeholderText", u"Where"_s);
    QVERIFY(where);
    where->setProperty("text", u"The Cave"_s);
    QMetaObject::invokeMethod(where, "textEdited");
    QQuickItem *save = find(overlay, "StickerButton", "text", u"Save"_s);
    QVERIFY(save);
    QTest::qWait(100);
    click(window, save);

    QTRY_COMPARE(named(source, u"Bouldering"_s).location, u"The Cave"_s);
    // Climbing does not repeat, so nothing asked which occurrences.
    QVERIFY(named(source, u"Climbing"_s).summary.isEmpty());
}

QTEST_MAIN(TestEventEdit)
#include "tst_eventedit.moc"
