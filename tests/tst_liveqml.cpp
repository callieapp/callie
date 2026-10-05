#include "LiveQml.h"

#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

void write(const QString &path, const QByteArray &content)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(content);
}

} // namespace

class TestLiveQml : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void savedEditReloads();
    void geometrySurvivesAFailedLoad();
    void otherFilesDoNotReload();
};

void TestLiveQml::savedEditReloads()
{
    // A module whose generated qmldir prefers resources that do not exist, as
    // a compiled module's does; live mode must ignore that line.
    QTemporaryDir source;
    QTemporaryDir build;
    write(build.filePath(u"qmldir"_s),
          "module Test.Live\nprefer :/qt/qml/Test/Live/\nThing 1.0 Thing.qml\n");
    write(source.filePath(u"Thing.qml"_s), "import QtQml\nQtObject { property int value: 1 }\n");

    QQmlApplicationEngine engine;
    int loads = 0;
    LiveQml live(engine, {{u"Test.Live"_s, source.path(), build.path()}}, [&] {
        ++loads;
        engine.loadFromModule("Test.Live", "Thing");
    });
    QVERIFY2(live.start(), qPrintable(live.errorString()));
    engine.loadFromModule("Test.Live", "Thing");
    QCOMPARE(engine.rootObjects().size(), 1);
    QCOMPARE(engine.rootObjects().first()->property("value").toInt(), 1);

    write(source.filePath(u"Thing.qml"_s), "import QtQml\nQtObject { property int value: 2 }\n");

    QTRY_COMPARE_WITH_TIMEOUT(loads, 1, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(engine.rootObjects().size(), 1, 3000);
    QCOMPARE(engine.rootObjects().last()->property("value").toInt(), 2);

    // A file added while running is picked up too.
    write(source.filePath(u"Other.qml"_s), "import QtQml\nQtObject {}\n");
    QTRY_COMPARE_WITH_TIMEOUT(loads, 2, 3000);
}

namespace {

/// A module like the app's, with one window type, served live.
struct LiveModule
{
    LiveModule()
    {
        write(build.filePath(u"qmldir"_s), "module Test.Window\nWin 1.0 Win.qml\n");
        save("Window { width: 300; height: 200 }");
    }

    void save(const QByteArray &body)
    {
        write(source.filePath(u"Win.qml"_s), "import QtQuick\n" + body + "\n");
    }

    QTemporaryDir source;
    QTemporaryDir build;
};

} // namespace

void TestLiveQml::geometrySurvivesAFailedLoad()
{
    LiveModule module;
    QQmlApplicationEngine engine;
    int loads = 0;
    LiveQml live(engine, {{u"Test.Window"_s, module.source.path(), module.build.path()}}, [&] {
        ++loads;
        engine.loadFromModule("Test.Window", "Win");
    });
    QVERIFY(live.start());
    engine.loadFromModule("Test.Window", "Win");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    window->setGeometry(40, 50, 640, 480);

    module.save("Window { width: 300; height: 200; this is not qml");
    QTRY_COMPARE_WITH_TIMEOUT(loads, 1, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(engine.rootObjects().isEmpty(), 3000);

    module.save("Window { width: 300; height: 200 }");
    QTRY_COMPARE_WITH_TIMEOUT(loads, 2, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(engine.rootObjects().size(), 1, 3000);
    window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QCOMPARE(window->size(), QSize(640, 480));
}

void TestLiveQml::otherFilesDoNotReload()
{
    LiveModule module;
    QQmlApplicationEngine engine;
    int loads = 0;
    LiveQml live(engine, {{u"Test.Window"_s, module.source.path(), module.build.path()}}, [&] {
        ++loads;
        engine.loadFromModule("Test.Window", "Win");
    });
    QVERIFY(live.start());
    engine.loadFromModule("Test.Window", "Win");

    write(module.source.filePath(u".Win.qml.swp"_s), "editor state");
    write(module.source.filePath(u"Win.qml~"_s), "backup");
    QTest::qWait(400);

    QCOMPARE(loads, 0);
}

QTEST_MAIN(TestLiveQml)
#include "tst_liveqml.moc"
