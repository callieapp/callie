#include "callie/LookedSource.h"
#include "callie/SampleSource.h"
#include "callie/Settings.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestLookedSource : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void calendarsAndEventsTakeTheUsersLook();
    void backgroundLoadsTakeItToo();
    void changingALookReloads();
};

namespace {

QDateTime monday()
{
    return QDateTime(QDate(2026, 10, 5), QTime(0, 0), QTimeZone::UTC);
}

} // namespace

void TestLookedSource::calendarsAndEventsTakeTheUsersLook()
{
    QTemporaryDir dir;
    Settings settings(dir.filePath(u"settings.ini"_s));
    SampleSource sample;
    LookedSource source(sample, settings);
    const QString work = sample.calendars().first().id;
    settings.setCalendarName(work, u"  Day job "_s);
    settings.setCalendarColor(work, QColor(u"#33b679"_s));

    const CalendarInfo calendar = source.calendars().first();
    QCOMPARE(calendar.displayName, u"Day job"_s);
    QCOMPARE(calendar.color, QColor(u"#33b679"_s));
    // Other calendars keep their own.
    QCOMPARE(source.calendars().last().displayName, sample.calendars().last().displayName);

    for (const Event &event : source.eventsBetween(monday(), monday().addDays(7), QTimeZone::UTC)) {
        if (event.calendarId == work)
            QCOMPARE(event.color, QColor(u"#33b679"_s));
    }

    // Clearing the name keeps the color.
    settings.setCalendarName(work, {});
    QCOMPARE(source.calendars().first().displayName, sample.calendars().first().displayName);
    QCOMPARE(source.calendars().first().color, QColor(u"#33b679"_s));
    settings.resetCalendarLook(work);
    QCOMPARE(source.calendars().first().color, sample.calendars().first().color);
}

void TestLookedSource::backgroundLoadsTakeItToo()
{
    QTemporaryDir dir;
    Settings settings(dir.filePath(u"settings.ini"_s));
    SampleSource sample;
    LookedSource source(sample, settings);
    const QString work = sample.calendars().first().id;
    settings.setCalendarColor(work, QColor(u"#8e24aa"_s));

    QFuture<SourceSnapshot> future = source.load(monday(), monday().addDays(7), QTimeZone::UTC);
    future.waitForFinished();
    const SourceSnapshot snapshot = future.result();
    QCOMPARE(snapshot.calendars.first().color, QColor(u"#8e24aa"_s));
    QVERIFY(std::any_of(snapshot.events.cbegin(), snapshot.events.cend(), [&work](const Event &e) {
        return e.calendarId == work && e.color == QColor(u"#8e24aa"_s);
    }));
}

void TestLookedSource::changingALookReloads()
{
    QTemporaryDir dir;
    Settings settings(dir.filePath(u"settings.ini"_s));
    SampleSource sample;
    LookedSource source(sample, settings);
    QSignalSpy changed(&source, &CalendarSource::changed);

    settings.setCalendarName(u"work"_s, u"Job"_s);
    QCOMPARE(changed.size(), 1);
    // The same name again is no change.
    settings.setCalendarName(u"work"_s, u"Job"_s);
    QCOMPARE(changed.size(), 1);
    sample.refresh();
    QCOMPARE(changed.size(), 2);
}

QTEST_GUILESS_MAIN(TestLookedSource)
#include "tst_lookedsource.moc"
