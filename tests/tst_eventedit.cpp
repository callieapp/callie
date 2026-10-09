#include "Clock.h"
#include "EventActions.h"
#include "EventModelForeign.h"
#include "ThemeController.h"

#include "callie/ContactBook.h"
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

#include <functional>

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
    void copyPastesUnderThePointer();
    void viKeysDriveTheWindow();
    void foundEventsOpenInTheViewShown();
    void agendaScrollsWithKeys();
    void clickOutsideOnlyCloses();
    void topOfACascadeOpens();
    void titleBarFitsNarrowWindows();
    void guestsComeFromSuggestions();
    void trayAsksWhichOccurrences();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<SampleSource> m_source;
    std::unique_ptr<ContactBook> m_contacts;
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
    m_contacts = std::make_unique<ContactBook>(QString());
    m_contacts->setSource(m_source.get());
    ContactBookForeign::s_instance = m_contacts.get();
    m_engine = std::make_unique<QQmlApplicationEngine>();
    m_engine->setInitialProperties(
        {{u"source"_s, QVariant::fromValue<CalendarSource *>(m_source.get())}});
    m_engine->loadFromModule("Callie.Ui", "Main");
    QVERIFY(!m_engine->rootObjects().isEmpty());
    m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
    m_window->resize(1280, 1300);
    QVERIFY(QTest::qWaitForWindowExposed(m_window));
    // Shortcuts only work in the active window.
    m_window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
}

void TestEventEdit::cleanup()
{
    m_engine.reset();
    ContactBookForeign::s_instance = nullptr;
    m_contacts.reset();
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

void TestEventEdit::copyPastesUnderThePointer()
{
    QQuickItem *block = nullptr;
    QTRY_VERIFY((block = find(m_window->contentItem(), "EventBlock", "summary", u"Climbing"_s)));
    QTest::qWait(300);
    const Event original = named(*m_source, u"Climbing"_s);
    const int hour = ThemeController::instance()->hourHeight();
    // The day before, two hours later: over the grid at the block's top edge.
    const QPoint target = block->mapToScene(QPointF(-block->width() / 2, 1 + 2 * hour)).toPoint();

    click(m_window, block);
    QTRY_VERIFY(find(m_window->contentItem(), "StickerButton", "text", u"Duplicate"_s));
    // Copying needs the card fully open.
    QTest::qWait(400);
    QTest::keySequence(m_window, QKeySequence::Copy);
    QTRY_VERIFY(!m_window->property("copiedEvent").isNull());
    QTest::keyClick(m_window, Qt::Key_Escape);
    QTest::mouseMove(m_window, target);
    QTest::qWait(100);
    QTest::keySequence(m_window, QKeySequence::Paste);

    const QDateTime from(QDate(2026, 10, 5), QTime(0, 0));
    QList<Event> copies;
    QTRY_VERIFY_WITH_TIMEOUT(
        [&] {
            copies.clear();
            for (const Event &e :
                 m_source->eventsBetween(from, from.addDays(7), QTimeZone::systemTimeZone())) {
                if (e.summary == u"Climbing" && e.eventId != original.eventId)
                    copies.append(e);
            }
            return copies.size() == 1;
        }(),
        5000);
    QCOMPARE(copies.first().start, original.start.addDays(-1).addSecs(2 * 3600));
    QCOMPARE(copies.first().end, original.end.addDays(-1).addSecs(2 * 3600));
}

void TestEventEdit::viKeysDriveTheWindow()
{
    m_settings->setViMode(true);
    m_settings->setLeaderTimeout(0);

    // The leader, a pause that shows what can follow it, then a view.
    QTest::keyClick(m_window, ',');
    QTRY_VERIFY(find(m_window->contentItem(), "WhichKey", "visible", u"true"_s));
    QTest::keyClick(m_window, 'm');
    QCOMPARE(m_settings->view(), u"month"_s);
    QVERIFY(!find(m_window->contentItem(), "WhichKey", "visible", u"true"_s));

    // Keys typed into a text field stay there.
    QTest::keyClick(m_window, '/');
    QQuickItem *search = nullptr;
    QTRY_VERIFY((search = m_window->activeFocusItem()) && search->inherits("QQuickTextInput"));
    QTest::keyClick(m_window, ',');
    QTest::keyClick(m_window, 'w');
    QCOMPARE(search->property("text").toString(), u",w"_s);
    QCOMPARE(m_settings->view(), u"month"_s);
    QTest::keyClick(m_window, Qt::Key_Escape);

    // Nor while an event's card is open.
    QQuickItem *block = nullptr;
    QTRY_VERIFY((block = find(m_window->contentItem(), "MonthChip", "visible", u"true"_s)));
    click(m_window, block);
    QTRY_VERIFY(find(m_window->contentItem(), "StickerButton", "text", u"Duplicate"_s));
    QTest::qWait(400);
    QTest::keyClick(m_window, ',');
    QTest::keyClick(m_window, 'w');
    QCOMPARE(m_settings->view(), u"month"_s);
    QTest::keyClick(m_window, Qt::Key_Escape);

    // With Space as the leader, a focused button still takes Space.
    m_settings->setLeaderKey(u" "_s);
    QQuickItem *today = find(m_window->contentItem(), "StickerButton", "text", u"Today"_s);
    QVERIFY(today);
    today->forceActiveFocus();
    QTest::keyClick(m_window, Qt::Key_Space);
    QObject *router = nullptr;
    for (QObject *o : m_window->findChildren<QObject *>()) {
        if (QString::fromLatin1(o->metaObject()->className()).startsWith(u"callie::KeyRouter"_s) &&
            o->property("window").value<QObject *>() == m_window)
            router = o;
    }
    QVERIFY(router);
    QCOMPARE(router->property("pending").toString(), QString());

    // Off again, the keys do nothing.
    m_settings->setViMode(false);
    QTRY_VERIFY(!m_window->activeFocusItem() ||
                !m_window->activeFocusItem()->inherits("QQuickTextInput"));
    QTest::keyClick(m_window, ',');
    QTest::keyClick(m_window, 'w');
    QCOMPARE(m_settings->view(), u"month"_s);
}

void TestEventEdit::foundEventsOpenInTheViewShown()
{
    // As a search result, or a reminder, asks to see the event.
    m_settings->setView(u"month"_s);
    const Event climbing = named(*m_source, u"Climbing"_s);
    QTRY_VERIFY(find(m_window->contentItem(), "MonthView", "visible", u"true"_s));
    QVERIFY(QMetaObject::invokeMethod(m_window, "revealEvent", Q_ARG(QVariant, climbing.start),
                                      Q_ARG(QVariant, climbing.uid),
                                      Q_ARG(QVariant, climbing.start)));
    QCOMPARE(m_settings->view(), u"month"_s);
    const auto cardOpen = [this] {
        for (QObject *o : m_window->findChildren<QObject *>()) {
            if (QString::fromLatin1(o->metaObject()->className()).startsWith(u"EventDetails"_s) &&
                o->property("opened").toBool())
                return o->property("summary").toString();
        }
        return QString();
    };
    QTRY_COMPARE(cardOpen(), u"Climbing"_s);
}

void TestEventEdit::agendaScrollsWithKeys()
{
    m_settings->setViMode(true);
    m_settings->setView(u"agenda"_s);
    QQuickItem *list = nullptr;
    QTRY_VERIFY((list = find(m_window->contentItem(), "QQuickListView", "visible", u"true"_s)));
    m_window->resize(1280, 400);
    QTest::qWait(200);
    const qreal top = list->property("contentY").toReal();
    QTest::keyClick(m_window, 'j');
    QTRY_VERIFY(list->property("contentY").toReal() > top);
    QTest::keyClick(m_window, 'k');
    QTRY_COMPARE(list->property("contentY").toReal(), top);
}

namespace {

/// The summary on the event card that is open, or empty.
QString openCard(QQuickWindow *window)
{
    for (QObject *o : window->findChildren<QObject *>()) {
        if (QString::fromLatin1(o->metaObject()->className()).startsWith(u"EventDetails"_s) &&
            o->property("opened").toBool())
            return o->property("summary").toString();
    }
    return {};
}

} // namespace

void TestEventEdit::clickOutsideOnlyCloses()
{
    QQuickItem *climbing = nullptr;
    QTRY_VERIFY((climbing = find(m_window->contentItem(), "EventBlock", "summary", u"Climbing"_s)));
    QQuickItem *lunch =
        find(m_window->contentItem(), "EventBlock", "summary", u"Lunch with Alex"_s);
    QVERIFY(lunch);
    QTest::qWait(300);
    click(m_window, climbing);
    QTRY_COMPARE(openCard(m_window), u"Climbing"_s);
    QTest::qWait(300);
    // Another event, outside the card: the card closes, and nothing else opens.
    click(m_window, lunch);
    QTRY_COMPARE(openCard(m_window), QString());
    QTest::qWait(500);
    QCOMPARE(openCard(m_window), QString());
}

void TestEventEdit::topOfACascadeOpens()
{
    // Wednesday's quick sync sits over the vendor call, over the roadmap workshop.
    QQuickItem *top = nullptr;
    QTRY_VERIFY(
        (top = find(m_window->contentItem(), "EventBlock", "summary", u"Quick sync w/ Sam"_s)));
    QTest::qWait(300);
    click(m_window, top);
    QTRY_COMPARE(openCard(m_window), u"Quick sync w/ Sam"_s);
    QTest::qWait(300);
    QCOMPARE(openCard(m_window), u"Quick sync w/ Sam"_s);
}

void TestEventEdit::titleBarFitsNarrowWindows()
{
    // Every control in the title bar, wherever it sits in the item tree.
    const std::function<void(QQuickItem *, QList<QRectF> &)> controls =
        [&controls](QQuickItem *item, QList<QRectF> &found) {
            const QString type = QString::fromLatin1(item->metaObject()->className());
            if (!item->isVisible() || item->width() <= 0)
                return;
            if (type.startsWith(u"StickerButton"_s) || type.startsWith(u"PillButton"_s) ||
                type.startsWith(u"QQuickAbstractButton"_s)) {
                const QRectF box = item->mapRectToScene(item->boundingRect());
                if (box.top() < 56)
                    found.append(box);
                return;
            }
            for (QQuickItem *child : item->childItems())
                controls(child, found);
        };
    // Each step's narrowest width, and the width just below it.
    for (const int width : {1280, 1200, 1199, 1000, 999, 880, 879, 780, 779, 600}) {
        m_window->resize(width, 600);
        QTest::qWait(100);
        QList<QRectF> boxes;
        controls(m_window->contentItem(), boxes);
        QVERIFY(boxes.size() >= 6);
        std::sort(boxes.begin(), boxes.end(),
                  [](const QRectF &a, const QRectF &b) { return a.left() < b.left(); });
        for (qsizetype i = 1; i < boxes.size(); ++i)
            QVERIFY2(boxes.at(i - 1).right() <= boxes.at(i).left() + 0.5,
                     qPrintable(
                         u"%1 px: controls overlap at x %2"_s.arg(width).arg(boxes.at(i).left())));
        QVERIFY(boxes.last().right() <= width);
    }

    // Paging leaves the arrows where they were, even with short month names.
    m_window->resize(800, 600);
    QTest::qWait(100);
    QQuickItem *next = find(m_window->contentItem(), "StickerButton", "glyph", u"chevron-right"_s);
    QVERIFY(next);
    m_settings->setView(u"month"_s);
    QTest::qWait(50);
    const qreal arrow = next->mapToScene(QPointF()).x();
    for (int month = 0; month < 12; ++month) {
        click(m_window, next);
        QTest::qWait(50);
        QCOMPARE(next->mapToScene(QPointF()).x(), arrow);
    }

    // Narrow, the sidebar is a button away.
    QQuickItem *menu = find(m_window->contentItem(), "StickerButton", "glyph", u"menu"_s);
    QVERIFY(menu);
    click(m_window, menu);
    QTRY_VERIFY([this] {
        for (QObject *o : m_window->findChildren<QObject *>()) {
            if (QString::fromLatin1(o->metaObject()->className()).startsWith(u"Sidebar"_s) &&
                o->property("visible").toBool())
                return true;
        }
        return false;
    }());
    // Wide again, the drawer closes and the sidebar is back in its place.
    auto *drawer = m_window->findChild<QObject *>(u"sidebarDrawer"_s);
    QVERIFY(drawer);
    QVERIFY(drawer->property("visible").toBool());
    m_window->resize(1280, 600);
    QTRY_VERIFY(!drawer->property("visible").toBool());
    QTRY_VERIFY(!find(m_window->contentItem(), "StickerButton", "glyph", u"menu"_s));
}

void TestEventEdit::guestsComeFromSuggestions()
{
    QQuickItem *form = openEditor(u"Climbing"_s);
    QVERIFY(form);
    QQuickItem *guest =
        find(m_window->contentItem(), "Field", "placeholderText", u"Add guests by name or email"_s);
    QVERIFY(guest);
    // Priya is a guest at the sample's calls, so typing her name finds her.
    QTRY_VERIFY(!m_contacts->suggest(u"pri"_s, {}).isEmpty());
    guest->setProperty("text", u"pri"_s);
    QMetaObject::invokeMethod(guest, "textEdited");
    QTRY_VERIFY(!guest->property("suggestions").toList().isEmpty());
    QMetaObject::invokeMethod(guest, "accepted");
    QCOMPARE(form->property("guests").toStringList(), QStringList{u"priya@example.com"_s});
    QCOMPARE(guest->property("text").toString(), QString());

    // A name that matches no one is not taken for an address.
    guest->setProperty("text", u"Nobody Here"_s);
    QMetaObject::invokeMethod(guest, "textEdited");
    QMetaObject::invokeMethod(guest, "accepted");
    QCOMPARE(form->property("guests").toStringList(), QStringList{u"priya@example.com"_s});
    QCOMPARE(guest->property("text").toString(), u"Nobody Here"_s);

    // A whole address that matches no one goes in as typed.
    guest->setProperty("text", u"sam@new.example"_s);
    QMetaObject::invokeMethod(guest, "textEdited");
    QMetaObject::invokeMethod(guest, "accepted");
    QCOMPARE(form->property("guests").toStringList(),
             (QStringList{u"priya@example.com"_s, u"sam@new.example"_s}));
}

void TestEventEdit::trayAsksWhichOccurrences()
{
    for (QObject *o : m_window->findChildren<QObject *>()) {
        if (QString::fromLatin1(o->metaObject()->className()).startsWith(u"InvitesTray"_s))
            QVERIFY(QMetaObject::invokeMethod(o, "open"));
    }
    // Berlin sync, a repeating invitation, comes first.
    QQuickItem *yes = nullptr;
    QTRY_VERIFY((yes = find(m_window->contentItem(), "PillButton", "label", u"Yes"_s)));
    QTest::qWait(300);
    click(m_window, yes);
    QQuickItem *one = nullptr;
    QTRY_VERIFY((one = find(m_window->contentItem(), "PillButton", "label", u"This event"_s)));
    QVERIFY(find(m_window->contentItem(), "PillButton", "label", u"All events"_s));
    // Past the double-click interval, so the second click is a click of its own.
    QTest::qWait(600);
    click(m_window, one);

    const auto answer = [this](QDate day) {
        const QDateTime from(day, QTime(0, 0));
        for (const Event &e :
             m_source->eventsBetween(from, from.addDays(1), QTimeZone::systemTimeZone())) {
            if (e.summary == u"Berlin sync")
                return e.responseStatus;
        }
        return QString();
    };
    // Only the next one is answered; the series still waits.
    QTRY_COMPARE(answer(QDate(2026, 10, 13)), u"accepted"_s);
    QCOMPARE(answer(QDate(2026, 10, 20)), u"needsAction"_s);
}

QTEST_MAIN(TestEventEdit)
#include "tst_eventedit.moc"
