#include "callie/Settings.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestSettings : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void defaultsWithoutAFile();
    void choicesSurviveARestart();
    void unchangedValueEmitsNothing();
    void unknownZoneFollowsTheSystem();
    void unknownStoredZoneFollowsTheSystem();
    void timesFollowTheChosenFormat();
    void calendarsHideAndShow();
    void resetForgetsEverything();
    void timesAreReplacedOnChange();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    QString path() const { return m_dir->filePath(u"callie/settings.ini"_s); }
};

void TestSettings::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
}

void TestSettings::defaultsWithoutAFile()
{
    const Settings settings(path());
    QCOMPARE(settings.timeFormat(), Settings::TimeFormat::Locale);
    QVERIFY(settings.timeZoneId().isEmpty());
    QCOMPARE(settings.timeZone(), QTimeZone::systemTimeZone());
    QVERIFY(settings.showDeclined());
    QVERIFY(settings.dimPast());
    QVERIFY(!settings.widenToday());
    QVERIFY(settings.hiddenCalendars().isEmpty());
}

void TestSettings::choicesSurviveARestart()
{
    {
        Settings settings(path());
        settings.setTimeFormat(Settings::TimeFormat::TwelveHour);
        settings.setTimeZoneId(u"Asia/Tokyo"_s);
        settings.setShowDeclined(false);
        settings.setDimPast(false);
        settings.setWidenToday(true);
        settings.setCalendarVisible(u"google/me/work"_s, false);
        settings.setAccountCollapsed(u"me@example.com"_s, true);
    }
    const Settings settings(path());
    QCOMPARE(settings.timeFormat(), Settings::TimeFormat::TwelveHour);
    QCOMPARE(settings.timeZone(), QTimeZone("Asia/Tokyo"));
    QVERIFY(!settings.showDeclined());
    QVERIFY(!settings.dimPast());
    QVERIFY(settings.widenToday());
    QCOMPARE(settings.hiddenCalendars(), QStringList{u"google/me/work"_s});
    QCOMPARE(settings.collapsedAccounts(), QStringList{u"me@example.com"_s});
}

void TestSettings::unchangedValueEmitsNothing()
{
    Settings settings(path());
    QSignalSpy dim(&settings, &Settings::dimPastChanged);
    settings.setDimPast(true);
    QCOMPARE(dim.size(), 0);
    settings.setDimPast(false);
    QCOMPARE(dim.size(), 1);
}

void TestSettings::unknownZoneFollowsTheSystem()
{
    Settings settings(path());
    settings.setTimeZoneId(u"Europe/Berlin"_s);
    settings.setTimeZoneId(u"Mars/Olympus_Mons"_s);
    QVERIFY(settings.timeZoneId().isEmpty());
    QCOMPARE(settings.timeZone(), QTimeZone::systemTimeZone());
}

void TestSettings::unknownStoredZoneFollowsTheSystem()
{
    {
        QSettings file(path(), QSettings::IniFormat);
        file.setValue(u"time/zone"_s, u"Mars/Olympus_Mons"_s);
    }
    const Settings settings(path());
    QVERIFY(settings.timeZoneId().isEmpty());
    QVERIFY(settings.timeZone().isValid());
}

void TestSettings::timesFollowTheChosenFormat()
{
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    Settings settings(path());
    settings.setTimeZoneId(u"UTC"_s);
    const QDateTime time(QDate(2026, 10, 6), QTime(14, 30), QTimeZone::UTC);

    QVERIFY(!settings.use24Hour()); // en_US uses a 12-hour clock
    QCOMPARE(settings.times()->time(time), u"2:30 PM"_s);
    QCOMPARE(settings.times()->hour(9), u"9 AM"_s);

    settings.setTimeFormat(Settings::TimeFormat::TwentyFourHour);
    QCOMPARE(settings.times()->time(time), u"14:30"_s);
    QCOMPARE(settings.times()->hour(9), u"9:00"_s);

    // The chosen zone moves both the time and the date.
    settings.setTimeZoneId(u"Asia/Tokyo"_s);
    QCOMPARE(settings.times()->time(time), u"23:30"_s);
    QCOMPARE(settings.times()->minutesIntoDay(time), 23 * 60 + 30);
    QCOMPARE(settings.times()->date(time.addSecs(3600)), QDate(2026, 10, 7).startOfDay());

    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
    settings.setTimeFormat(Settings::TimeFormat::Locale);
    QVERIFY(settings.use24Hour());
    QLocale::setDefault(QLocale::c());
}

void TestSettings::calendarsHideAndShow()
{
    Settings settings(path());
    QSignalSpy hidden(&settings, &Settings::hiddenCalendarsChanged);
    settings.setCalendarVisible(u"a"_s, false);
    settings.setCalendarVisible(u"a"_s, false);
    settings.setCalendarVisible(u"b"_s, false);
    settings.setCalendarVisible(u"a"_s, true);
    QCOMPARE(settings.hiddenCalendars(), QStringList{u"b"_s});
    QCOMPARE(hidden.size(), 3);
}

void TestSettings::resetForgetsEverything()
{
    Settings settings(path());
    settings.setWidenToday(true);
    settings.setTimeZoneId(u"Asia/Tokyo"_s);
    QSignalSpy widen(&settings, &Settings::widenTodayChanged);
    QSignalSpy declined(&settings, &Settings::showDeclinedChanged);

    settings.reset();

    QVERIFY(!settings.widenToday());
    QVERIFY(settings.timeZoneId().isEmpty());
    QCOMPARE(widen.size(), 1);
    QCOMPARE(declined.size(), 0);
    QVERIFY(!Settings(path()).widenToday());
}

void TestSettings::timesAreReplacedOnChange()
{
    Settings settings(path());
    QSignalSpy times(&settings, &Settings::timesChanged);
    const Times *before = settings.times();

    settings.setDimPast(false);
    QCOMPARE(times.size(), 0);
    settings.setTimeZoneId(u"Asia/Tokyo"_s);
    QCOMPARE(times.size(), 1);
    QVERIFY(settings.times() != before);
    QCOMPARE(settings.times()->zone(), QTimeZone("Asia/Tokyo"));
}

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
