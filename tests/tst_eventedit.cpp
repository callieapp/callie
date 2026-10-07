#include "Clock.h"
#include "EventActions.h"
#include "EventModelForeign.h"

#include "callie/SampleSource.h"
#include "callie/Settings.h"

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
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
    void initTestCase() { QQuickStyle::setStyle(u"Basic"_s); }
    void init();
    void cleanup();
    void cardTurnsIntoAFormAndSaves();
    void movedSeriesGetsItsRuleRewritten();
    void overnightKeepsItsLength();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<SampleSource> m_source;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_window = nullptr;

    /// Clicks the event, then Edit on its card, and gives back the form.
    QQuickItem *openEditor(const QString &summary)
    {
        QQuickItem *block = nullptr;
        if (!QTest::qWaitFor([&] {
                return (block = find(m_window->contentItem(), "EventBlock", "summary", summary));
            }))
            return nullptr;
        QTest::qWait(300);
        click(m_window, block);
        QQuickItem *edit = nullptr;
        if (!QTest::qWaitFor([&] {
                return (edit = find(m_window->contentItem(), "StickerButton", "text", u"Edit"_s));
            }))
            return nullptr;
        QTest::qWait(300);
        click(m_window, edit);
        QQuickItem *form = nullptr;
        return QTest::qWaitFor([&] {
            return (form = find(m_window->contentItem(), "EventEditor", "title", summary));
        })
                   ? form
                   : nullptr;
    }
};

void TestEventEdit::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_settings = std::make_unique<Settings>(m_dir->filePath(u"s.ini"_s));
    SettingsForeign::s_instance = m_settings.get();
    Clock::instance()->freeze(QDateTime(QDate(2026, 10, 7), QTime(13, 40)));
    m_source = std::make_unique<SampleSource>();
    m_engine = std::make_unique<QQmlApplicationEngine>();
    m_engine->setInitialProperties(
        {{u"source"_s, QVariant::fromValue<CalendarSource *>(m_source.get())}});
    m_engine->loadFromModule("Callie.Ui", "Main");
    QVERIFY(!m_engine->rootObjects().isEmpty());
    m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
    m_window->resize(1280, 1300);
    QVERIFY(QTest::qWaitForWindowExposed(m_window));
}

void TestEventEdit::cleanup()
{
    m_engine.reset();
    m_source.reset();
}

void TestEventEdit::cardTurnsIntoAFormAndSaves()
{
    // Popups live in the overlay, which is under the window's content too.
    QQuickItem *overlay = m_window->contentItem();
    QVERIFY(openEditor(u"Climbing"_s));

    // A new title and place, saved.
    QQuickItem *title = find(overlay, "Field", "placeholderText", u"Title"_s);
    QVERIFY(title);
    title->setProperty("text", u"Bouldering"_s);
    QMetaObject::invokeMethod(title, "textEdited");
    QQuickItem *where = find(overlay, "Field", "placeholderText", u"Where"_s);
    QVERIFY(where);
    where->setProperty("text", u"The Cave"_s);
    QMetaObject::invokeMethod(where, "textEdited");
    QQuickItem *save = find(overlay, "StickerButton", "text", u"Save"_s);
    QVERIFY(save);
    QTest::qWait(100);
    click(m_window, save);

    QTRY_COMPARE(named(*m_source, u"Bouldering"_s).location, u"The Cave"_s);
    // Climbing does not repeat, so nothing asked which occurrences.
    QVERIFY(named(*m_source, u"Climbing"_s).summary.isEmpty());
}

void TestEventEdit::movedSeriesGetsItsRuleRewritten()
{
    QQuickItem *form = openEditor(u"Standup"_s);
    QVERIFY(form);
    const QDateTime day = form->property("startDay").toDateTime();
    const QDateTime end = form->property("endDay").toDateTime();

    // A day later, across a week boundary or not, the rule names the new day.
    form->setProperty("startDay", day.date().addDays(1).startOfDay());
    form->setProperty("endDay", end.date().addDays(1).startOfDay());
    const auto changes = [form](const QString &scope) {
        QVariant result;
        QMetaObject::invokeMethod(form, "changes", Q_RETURN_ARG(QVariant, result),
                                  Q_ARG(QVariant, scope));
        return result.toMap();
    };
    const QString code = QLocale(QLocale::C)
                             .dayName(day.date().addDays(1).dayOfWeek(), QLocale::ShortFormat)
                             .left(2)
                             .toUpper();
    QCOMPARE(changes(u"all"_s).value(u"recurrence"_s).toStringList(),
             QStringList{u"RRULE:FREQ=WEEKLY;BYDAY="_s + code});
    // One occurrence moves alone, leaving the rule.
    QVERIFY(!changes(u"this"_s).contains(u"recurrence"_s));
    QVERIFY(changes(u"this"_s).contains(u"start"_s));

    // So every choice is offered.
    QVERIFY(QMetaObject::invokeMethod(form, "save"));
    QTRY_VERIFY(find(m_window->contentItem(), "StickerButton", "text", u"All events"_s));
    QVERIFY(find(m_window->contentItem(), "StickerButton", "text", u"This event"_s));
    // A new repeat belongs to the series, so one occurrence cannot take it,
    // not even by saving again while asked.
    form->setProperty("repeat", u"daily"_s);
    QTRY_VERIFY(!find(m_window->contentItem(), "StickerButton", "text", u"This event"_s));
    std::vector<std::unique_ptr<QSignalSpy>> updates;
    for (EventActions *actions : m_window->findChildren<EventActions *>())
        updates.push_back(std::make_unique<QSignalSpy>(actions, &EventActions::updated));
    QVERIFY(!updates.empty());
    QVERIFY(QMetaObject::invokeMethod(form, "save"));
    QVERIFY(form->property("asking").toBool());
    for (const auto &spy : updates)
        QVERIFY(spy->isEmpty());

    // An end before the start cannot be saved.
    form->setProperty("endDay", day.date().addDays(-1).startOfDay());
    QVariant valid;
    QVERIFY(QMetaObject::invokeMethod(form, "valid", Q_RETURN_ARG(QVariant, valid)));
    QCOMPARE(valid, QVariant(false));
}

void TestEventEdit::overnightKeepsItsLength()
{
    QQuickItem *form = openEditor(u"Climbing"_s);
    QVERIFY(form);
    // 22:00 to 02:00 the next day, then started an hour later.
    const QDateTime day = form->property("startDay").toDateTime();
    form->setProperty("startMinutes", 22 * 60);
    form->setProperty("endDay", day.date().addDays(1).startOfDay());
    form->setProperty("endMinutes", 2 * 60);
    QVERIFY(QMetaObject::invokeMethod(form, "moveStart", Q_ARG(QVariant, 23 * 60)));
    QCOMPARE(form->property("endDay").toDateTime().date(), day.date().addDays(1));
    QCOMPARE(form->property("endMinutes").toInt(), 3 * 60);
}

QTEST_MAIN(TestEventEdit)
#include "tst_eventedit.moc"
