#include "LiveQml.h"

#include <QFile>
#include <QQmlApplicationEngine>
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

QTEST_MAIN(TestLiveQml)
#include "tst_liveqml.moc"
