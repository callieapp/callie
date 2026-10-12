#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

#include <functional>

Q_IMPORT_QML_PLUGIN(Callie_UiPlugin)

using namespace Qt::StringLiterals;

class TestDropdown : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { QQuickStyle::setStyle(u"Basic"_s); }
    void pickingTheCurrentChoiceKeepsItMarked();
    void aLongListFitsTheWindow();

private:
    /// The menu's entries, in the overlay over the window's content.
    static QList<QQuickItem *> entries(QQuickWindow &window);
    static void click(QQuickWindow &window, QQuickItem *item);
};

QList<QQuickItem *> TestDropdown::entries(QQuickWindow &window)
{
    QList<QQuickItem *> found;
    const std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
        if (item->isVisible() && item->property("marks").toBool())
            found << item;
        for (QQuickItem *child : item->childItems())
            walk(child);
    };
    walk(window.contentItem());
    return found;
}

void TestDropdown::click(QQuickWindow &window, QQuickItem *item)
{
    const QPointF center = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    QTest::mouseClick(&window, Qt::LeftButton, {}, center.toPoint());
}

void TestDropdown::pickingTheCurrentChoiceKeepsItMarked()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.loadFromModule("Callie.Ui", "Dropdown");
    const QVariantList model{QVariantMap{{u"id"_s, u"week"_s}, {u"name"_s, u"Week"_s}},
                             QVariantMap{{u"id"_s, u"month"_s}, {u"name"_s, u"Month"_s}}};
    std::unique_ptr<QObject> object(component.createWithInitialProperties(
        {{u"model"_s, model}, {u"currentValue"_s, u"week"_s}}));
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *dropdown = qobject_cast<QQuickItem *>(object.get());
    QQuickWindow window;
    window.resize(400, 300);
    dropdown->setParentItem(window.contentItem());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const auto marked = [&window] {
        QStringList names;
        for (QQuickItem *entry : entries(window)) {
            if (entry->property("current").toBool())
                names << entry->property("text").toString();
        }
        return names;
    };

    // Picking the current choice again leaves it marked as current.
    for (int round = 0; round < 2; ++round) {
        click(window, dropdown);
        QTRY_COMPARE(entries(window).size(), 2);
        QCOMPARE(marked(), QStringList{u"Week"_s});
        click(window, entries(window).first());
        QTRY_COMPARE(entries(window).size(), 0);
    }
    click(window, dropdown);
    QTRY_COMPARE(entries(window).size(), 2);
    QCOMPARE(marked(), QStringList{u"Week"_s});
}

void TestDropdown::aLongListFitsTheWindow()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.loadFromModule("Callie.Ui", "Dropdown");
    // As many choices as a user with a hundred calendars has.
    QVariantList model;
    for (int i = 0; i < 120; ++i)
        model << QVariantMap{{u"id"_s, QString::number(i)}, {u"name"_s, u"Calendar %1"_s.arg(i)}};
    std::unique_ptr<QObject> object(
        component.createWithInitialProperties({{u"model"_s, model}, {u"currentValue"_s, u"0"_s}}));
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *dropdown = qobject_cast<QQuickItem *>(object.get());
    QQuickWindow window;
    window.resize(400, 300);
    dropdown->setParentItem(window.contentItem());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    click(window, dropdown);
    QTRY_VERIFY(!entries(window).isEmpty());
    // The menu stays inside the window and scrolls to the rest.
    QQuickItem *first = entries(window).first();
    QQuickItem *list = first->parentItem();
    while (list && !list->inherits("QQuickListView"))
        list = list->parentItem();
    QVERIFY(list);
    const QRectF shown = list->mapRectToScene(QRectF(0, 0, list->width(), list->height()));
    QVERIFY2(shown.top() >= 0 && shown.bottom() <= window.height(),
             qPrintable(u"%1 to %2"_s.arg(shown.top()).arg(shown.bottom())));
    QVERIFY(list->property("contentHeight").toReal() > list->height());
    QVERIFY(list->property("interactive").toBool());
}

QTEST_MAIN(TestDropdown)
#include "tst_dropdown.moc"
