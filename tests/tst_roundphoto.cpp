#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(Callie_UiPlugin)

using namespace Qt::StringLiterals;

class TestRoundPhoto : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void drawsThePhotoInACircle();
    void drawsNothingWithoutAPhoto();
};

void TestRoundPhoto::drawsThePhotoInACircle()
{
    QTest::failOnWarning(QRegularExpression(u"drawImage"_s));
    QTemporaryDir dir;
    QImage red(64, 64, QImage::Format_ARGB32);
    red.fill(Qt::red);
    const QString file = dir.filePath(u"photo.png"_s);
    QVERIFY(red.save(file));

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.loadFromModule("Callie.Ui", "RoundPhoto");
    std::unique_ptr<QObject> object(
        component.createWithInitialProperties({{u"source"_s, QUrl::fromLocalFile(file)}}));
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *photo = qobject_cast<QQuickItem *>(object.get());
    QQuickWindow window;
    window.setColor(Qt::black);
    window.resize(64, 64);
    photo->setSize({64, 64});
    photo->setParentItem(window.contentItem());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QTRY_VERIFY(photo->property("ready").toBool());
    // The photo fills the circle, and the corners stay clear.
    QTRY_COMPARE(window.grabWindow().pixelColor(32, 32), QColor(Qt::red));
    QCOMPARE(window.grabWindow().pixelColor(1, 1), QColor(Qt::black));
}

void TestRoundPhoto::drawsNothingWithoutAPhoto()
{
    QTest::failOnWarning(QRegularExpression(u"drawImage"_s));
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.loadFromModule("Callie.Ui", "RoundPhoto");
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *photo = qobject_cast<QQuickItem *>(object.get());
    QQuickWindow window;
    window.setColor(Qt::black);
    window.resize(64, 64);
    photo->setSize({64, 64});
    photo->setParentItem(window.contentItem());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    // An account without a photo shows its letter instead.
    QMetaObject::invokeMethod(photo, "requestPaint");
    QCOMPARE(window.grabWindow().pixelColor(32, 32), QColor(Qt::black));
    QVERIFY(!photo->property("ready").toBool());
}

QTEST_MAIN(TestRoundPhoto)
#include "tst_roundphoto.moc"
