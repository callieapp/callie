#include "Clock.h"

#include <QSignalSpy>
#include <QTest>

using namespace callie;

class TestClock : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void followsTheRealTime();
    void frozenClockStaysPut();
};

void TestClock::followsTheRealTime()
{
    // The singleton is shared, so this runs before anything freezes it.
    const QDateTime before = QDateTime::currentDateTime();
    QVERIFY(Clock::instance()->now() >= before);
    QVERIFY(Clock::instance()->now() <= QDateTime::currentDateTime());
}

void TestClock::frozenClockStaysPut()
{
    const QDateTime moment(QDate(2026, 3, 18), QTime(10, 40));
    QSignalSpy changed(Clock::instance(), &Clock::nowChanged);

    Clock::instance()->freeze(moment);
    QTest::qWait(50);

    QCOMPARE(changed.size(), 1);
    QCOMPARE(Clock::instance()->now(), moment);
}

QTEST_GUILESS_MAIN(TestClock)
#include "tst_clock.moc"
