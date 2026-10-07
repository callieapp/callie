#include "callie/SampleSource.h"

#include <QSignalSpy>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestSampleSource : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void countsAsSyncedFromTheStart();
    void reportListsEachAccount();
    void syncTimeFollowsTheClock();
    void answersReachTheGuestList();
};

void TestSampleSource::countsAsSyncedFromTheStart()
{
    SampleSource source;
    QVERIFY(source.lastSynced().isValid());
    const QDateTime first = source.lastSynced();
    QSignalSpy status(&source, &CalendarSource::statusChanged);
    QTest::qWait(5);

    source.refresh();

    QCOMPARE(status.size(), 1);
    QVERIFY(source.lastSynced() > first);
}

void TestSampleSource::reportListsEachAccount()
{
    const SampleSource source;
    const QVariantList report = source.syncReport();

    // Work and Focus share an account; Personal has its own.
    QCOMPARE(report.size(), 2);
    const QVariantMap work = report.at(0).toMap();
    QCOMPARE(work.value(u"account"_s).toString(), u"sam@work.example"_s);
    QCOMPARE(work.value(u"lastSynced"_s).toDateTime(), source.lastSynced());
}

void TestSampleSource::syncTimeFollowsTheClock()
{
    SampleSource source;
    QDateTime now(QDate(2026, 10, 7), QTime(13, 40), QTimeZone::UTC);
    source.setNow([&now] { return now; });
    QCOMPARE(source.lastSynced(), now);

    now = now.addSecs(60);
    source.refresh();
    QCOMPARE(source.lastSynced(), now);
}

void TestSampleSource::answersReachTheGuestList()
{
    SampleSource source;
    const QDateTime from(QDate(2026, 10, 5), QTime(0, 0), QTimeZone::UTC);
    const auto invitation = [&source, &from] {
        for (const Event &e : source.eventsBetween(from, from.addDays(7), QTimeZone::UTC)) {
            if (e.summary == u"Vendor call")
                return e;
        }
        return Event{};
    };
    const auto self = [](const Event &e) {
        for (const Guest &guest : e.guests) {
            if (guest.self)
                return guest.response;
        }
        return QString();
    };
    const Event before = invitation();
    QCOMPARE(before.responseStatus, u"needsAction"_s);
    QCOMPARE(self(before), u"needsAction"_s);

    source.respond(before, u"accepted"_s, false, [](const QString &) {});
    const Event after = invitation();
    QCOMPARE(after.responseStatus, u"accepted"_s);
    QCOMPARE(self(after), u"accepted"_s);
}

QTEST_GUILESS_MAIN(TestSampleSource)
#include "tst_samplesource.moc"
