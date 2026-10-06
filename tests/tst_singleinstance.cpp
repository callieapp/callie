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

QTEST_GUILESS_MAIN(TestSingleInstance)
#include "tst_singleinstance.moc"
