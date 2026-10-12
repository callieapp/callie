#include "SingleInstance.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QSignalSpy>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestSingleInstance : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void secondLaunchActivatesTheFirst();
    void secondLaunchHandsOverItsFiles();
};

void TestSingleInstance::secondLaunchActivatesTheFirst()
{
    if (!QDBusConnection::sessionBus().isConnected())
        QSKIP("no session bus");
    // A name of its own, so a running Callie is left alone.
    const QString service = u"org.callieapp.CallieTest%1"_s.arg(QCoreApplication::applicationPid());

    SingleInstance first(service, QDBusConnection::sessionBus());
    QVERIFY(first.claim());
    QSignalSpy activated(&first, &SingleInstance::activated);

    // The second launch runs on a connection of its own, like another process.
    const QDBusConnection other =
        QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"second"_s);
    {
        SingleInstance second(service, other);
        QVERIFY(!second.claim());
    }
    QTRY_COMPARE(activated.size(), 1);
    QDBusConnection::disconnectFromBus(u"second"_s);
}

void TestSingleInstance::secondLaunchHandsOverItsFiles()
{
    if (!QDBusConnection::sessionBus().isConnected())
        QSKIP("no session bus");
    const QString service =
        u"org.callieapp.CallieFiles%1"_s.arg(QCoreApplication::applicationPid());

    SingleInstance first(service, QDBusConnection::sessionBus());
    QVERIFY(first.claim());
    QSignalSpy activated(&first, &SingleInstance::activated);
    QSignalSpy opened(&first, &SingleInstance::opened);

    const QDBusConnection other =
        QDBusConnection::connectToBus(QDBusConnection::SessionBus, u"files"_s);
    {
        SingleInstance second(service, other);
        QVERIFY(!second.claim({u"file:///tmp/party.ics"_s}));
    }
    QTRY_COMPARE(opened.size(), 1);
    QCOMPARE(opened.first().first().toStringList(), QStringList{u"file:///tmp/party.ics"_s});
    // The window comes forward to show them.
    QCOMPARE(activated.size(), 1);
    QDBusConnection::disconnectFromBus(u"files"_s);
}

QTEST_GUILESS_MAIN(TestSingleInstance)
#include "tst_singleinstance.moc"
