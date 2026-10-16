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
    // The same renderer everywhere, and one that needs no GPU.
    void initTestCase() { QQuickWindow::setGraphicsApi(QSGRendererInterface::Software); }
    void drawsThePhotoInACircle();
    void drawsNothingWithoutAPhoto();

private:
    /// What the canvas holds, read back from it rather than from the screen.
    static QColor drawnAt(QObject *canvas, int x, int y);
    /// A RoundPhoto of `source` in a window of its own.
    struct Shown
    {
        QQmlEngine engine;
        std::unique_ptr<QObject> object;
        QQuickWindow window;
        QQuickItem *photo = nullptr;
    };
    static void show(Shown &shown, const QUrl &source);
};

QColor TestRoundPhoto::drawnAt(QObject *canvas, int x, int y)
{
    QString data;
    QMetaObject::invokeMethod(canvas, "toDataURL", Q_RETURN_ARG(QString, data),
                              Q_ARG(QString, u"image/png"_s));
    const QImage image = QImage::fromData(QByteArray::fromBase64(data.section(u',', 1).toLatin1()));
    return image.isNull() ? QColor() : image.pixelColor(x, y);
}

void TestRoundPhoto::show(Shown &shown, const QUrl &source)
{
    QQmlComponent component(&shown.engine);
    component.loadFromModule("Callie.Ui", "RoundPhoto");
    shown.object.reset(component.createWithInitialProperties({{u"source"_s, source}}));
    QVERIFY2(shown.object, qPrintable(component.errorString()));
    shown.photo = qobject_cast<QQuickItem *>(shown.object.get());
    shown.window.resize(64, 64);
    shown.photo->setSize({64, 64});
    shown.photo->setParentItem(shown.window.contentItem());
    shown.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&shown.window));
}

void TestRoundPhoto::drawsThePhotoInACircle()
{
    QTest::failOnWarning(QRegularExpression(u"drawImage"_s));
    QTemporaryDir dir;
    QImage red(64, 64, QImage::Format_ARGB32);
    red.fill(Qt::red);
    const QString file = dir.filePath(u"photo.png"_s);
    QVERIFY(red.save(file));

    Shown shown;
    show(shown, QUrl::fromLocalFile(file));
    QTRY_VERIFY(shown.photo->property("ready").toBool());
    // The photo fills the circle, and the corners stay clear.
    QTRY_COMPARE(drawnAt(shown.photo, 32, 32), QColor(Qt::red));
    QCOMPARE(drawnAt(shown.photo, 1, 1).alpha(), 0);

    // A photo that goes away leaves nothing behind for the letter to sit on.
    shown.photo->setProperty("source", QUrl());
    QVERIFY(!shown.photo->property("ready").toBool());
    QTRY_COMPARE(drawnAt(shown.photo, 32, 32).alpha(), 0);
}

void TestRoundPhoto::drawsNothingWithoutAPhoto()
{
    QTest::failOnWarning(QRegularExpression(u"drawImage"_s));
    Shown shown;
    show(shown, QUrl());
    // An account without a photo shows its letter instead.
    QTRY_VERIFY(drawnAt(shown.photo, 32, 32).isValid());
    QCOMPARE(drawnAt(shown.photo, 32, 32).alpha(), 0);
    QVERIFY(!shown.photo->property("ready").toBool());
}

QTEST_MAIN(TestRoundPhoto)
#include "tst_roundphoto.moc"
