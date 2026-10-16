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

class TestPopups : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { QQuickStyle::setStyle(u"Basic"_s); }
    void opensAndClosesWithItsButton_data();
    void opensAndClosesWithItsButton();
};

void TestPopups::opensAndClosesWithItsButton_data()
{
    QTest::addColumn<QString>("popup");

    // Each one opens as the window opens it, made the first time by a LazyPopup.
    for (const char *popup :
         {"AboutDialog { }", "ImportDialog { }", "KeyHelp { actions: [] }", "LookEditor { }",
          "MoveChoice { }", "SearchPopup { }", "SubscribeDialog { }", "ThemeEditor { }",
          "SettingsDialog { }", "UpdateDialog { }", "WhatsNewDialog { }"})
        QTest::newRow(popup) << QString::fromLatin1(popup);
}

void TestPopups::opensAndClosesWithItsButton()
{
    QFETCH(QString, popup);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(u"import QtQuick\nimport Callie.Ui\n"
                      u"Item { width: 1000; height: 800\n"
                      u"  LazyPopup { objectName: \"lazy\"; sourceComponent: %1 } }"_s.arg(popup)
                          .toUtf8(),
                      QUrl());
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *root = qobject_cast<QQuickItem *>(object.get());
    QQuickWindow window;
    window.resize(1000, 800);
    root->setParentItem(window.contentItem());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QObject *lazy = root->findChild<QObject *>(u"lazy"_s);
    QVERIFY(QMetaObject::invokeMethod(lazy, "open"));
    QObject *opened = lazy->property("popup").value<QObject *>();
    QVERIFY(opened);
    QTRY_VERIFY(opened->property("opened").toBool());
    // Drawn once, so the popup's columns have laid out what they hold.
    window.grabWindow();
    // Sized by the window: the LazyPopup it comes from has no size of its own.
    QVERIFY2(opened->property("height").toReal() > 0,
             qPrintable(u"height %1"_s.arg(opened->property("height").toReal())));

    // The X, among what the popup shows, closes it.
    auto *content = opened->property("contentItem").value<QQuickItem *>();
    QQuickItem *close = nullptr;
    const std::function<void(QQuickItem *)> find = [&](QQuickItem *item) {
        if (!close && item->isVisible() &&
            QString::fromLatin1(item->metaObject()->className()).startsWith(u"CloseButton"))
            close = item;
        for (QQuickItem *child : item->childItems())
            find(child);
    };
    find(content);
    QVERIFY2(close, "no close button");
    const QPointF center = close->mapToScene(QPointF(close->width() / 2, close->height() / 2));
    QTest::mouseClick(&window, Qt::LeftButton, {}, center.toPoint());
    QTRY_VERIFY(!opened->property("opened").toBool());
}

QTEST_MAIN(TestPopups)
#include "tst_popups.moc"
