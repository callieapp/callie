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
    void workingHoursStayInOrder();
    void accountPhotosAreKept();
    void newCalendarsStartAsTheirProviderShowsThem();
    void keyboardSettingsKeepToWhatWorks();
    void weekSettingsResetAndSignal();
    void googleFillsOnlyWhatIsUnset();
    void unchangedValueEmitsNothing();
    void unknownZoneFollowsTheSystem();
    void unknownStoredZoneFollowsTheSystem();
    void timesFollowTheChosenFormat();
    void calendarsHideAndShow();
    void resetForgetsEverything();
    void timesAreReplacedOnChange();
    void gridTimesAreWallClock();
    void shiftsKeepTheWallClock();

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
    QCOMPARE(settings.view(), u"week"_s);
    QVERIFY(settings.lastSeenVersion().isEmpty());
    QVERIFY(settings.notify());
    QCOMPARE(settings.reminderMinutes(), 10);
    QVERIFY(!settings.keepRunning());
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
        settings.setTheme(u"/home/me/.config/callie/themes/mine.toml"_s);
        settings.setView(u"month"_s);
        settings.setView(u"year"_s);
        settings.setDefaultCalendar(u"google/me/work"_s);
        settings.setCalendarColor(u"google/me/work"_s, QColor(u"#e67c73"_s));
        settings.setAccountName(u"me@example.com"_s, u"Work"_s);
        settings.setLastSeenVersion(u"0.1.0"_s);
        settings.setNotify(false);
        settings.setReminderMinutes(-5);
        settings.setKeepRunning(true);
        settings.setWeekStart(7);
        settings.setHideWeekends(true);
        settings.setWeekNumbers(true);
        settings.setWorkEnd(18 * 60);
        settings.setWorkStart(8 * 60);
    }
    const Settings settings(path());
    QCOMPARE(settings.timeFormat(), Settings::TimeFormat::TwelveHour);
    QCOMPARE(settings.timeZone(), QTimeZone("Asia/Tokyo"));
    QVERIFY(!settings.showDeclined());
    QVERIFY(!settings.dimPast());
    QVERIFY(settings.widenToday());
    QCOMPARE(settings.hiddenCalendars(), QStringList{u"google/me/work"_s});
    QCOMPARE(settings.collapsedAccounts(), QStringList{u"me@example.com"_s});
    QCOMPARE(settings.theme(), u"/home/me/.config/callie/themes/mine.toml"_s);
    QCOMPARE(settings.view(), u"month"_s);
    QCOMPARE(settings.defaultCalendar(), u"google/me/work"_s);
    QCOMPARE(settings.calendarLooks().value(u"google/me/work"_s).toMap().value(u"color"_s),
             u"#e67c73"_s);
    QCOMPARE(settings.accountName(u"me@example.com"_s), u"Work"_s);
    QCOMPARE(settings.accountName(u"other@example.com"_s), u"other@example.com"_s);
    QCOMPARE(settings.lastSeenVersion(), u"0.1.0"_s);
    QVERIFY(!settings.notify());
    QCOMPARE(settings.reminderMinutes(), -1);
    QVERIFY(settings.keepRunning());
    QCOMPARE(settings.firstDayOfWeek(), int(Qt::Sunday));
    QVERIFY(settings.hideWeekends());
    QVERIFY(settings.weekNumbers());
    QCOMPARE(settings.workStart(), 8 * 60);
    QCOMPARE(settings.workEnd(), 18 * 60);
}

void TestSettings::weekSettingsResetAndSignal()
{
    Settings settings(path());
    QSignalSpy week(&settings, &Settings::weekChanged);
    QSignalSpy hours(&settings, &Settings::workHoursChanged);
    // The defaults again change nothing.
    settings.setWeekStart(0);
    settings.setHideWeekends(false);
    settings.setWeekNumbers(false);
    settings.setWorkStart(9 * 60);
    QCOMPARE(week.size(), 0);
    QCOMPARE(hours.size(), 0);

    settings.setWeekStart(7);
    settings.setWorkStart(8 * 60);
    settings.reset();
    QCOMPARE(week.size(), 2);
    QCOMPARE(hours.size(), 2);
    QCOMPARE(settings.weekStart(), 0);
    QCOMPARE(settings.workStart(), 9 * 60);
}

void TestSettings::googleFillsOnlyWhatIsUnset()
{
    Settings settings(path());
    settings.setShowDeclined(false);
    QSignalSpy week(&settings, &Settings::weekChanged);
    settings.seedFromGoogle({{u"weekStart"_s, u"0"_s},
                             {u"hideWeekends"_s, u"true"_s},
                             {u"format24HourTime"_s, u"true"_s},
                             {u"showDeclinedEvents"_s, u"true"_s},
                             {u"locale"_s, u"en"_s}});
    QCOMPARE(settings.firstDayOfWeek(), int(Qt::Sunday));
    QVERIFY(settings.hideWeekends());
    QCOMPARE(settings.timeFormat(), Settings::TimeFormat::TwentyFourHour);
    // Already chosen in Callie, so Google's choice does not override it.
    QVERIFY(!settings.showDeclined());
    QCOMPARE(week.size(), 2);

    // A second account changes nothing that is now set.
    settings.seedFromGoogle({{u"weekStart"_s, u"1"_s}, {u"hideWeekends"_s, u"false"_s}});
    QCOMPARE(settings.firstDayOfWeek(), int(Qt::Sunday));
    QVERIFY(settings.hideWeekends());

    // Even a choice matching Callie's default is kept from the first account.
    Settings fresh(m_dir->filePath(u"fresh.ini"_s));
    fresh.seedFromGoogle({{u"hideWeekends"_s, u"false"_s}});
    fresh.seedFromGoogle({{u"hideWeekends"_s, u"true"_s}});
    QVERIFY(!fresh.hideWeekends());
}

void TestSettings::workingHoursStayInOrder()
{
    Settings settings(path());
    QCOMPARE(settings.firstDayOfWeek(), int(QLocale().firstDayOfWeek()));
    QCOMPARE(settings.workStart(), 9 * 60);
    QCOMPARE(settings.workEnd(), 17 * 60);
    // A start past the end pushes the end along, and the reverse.
    settings.setWorkStart(17 * 60);
    QCOMPARE(settings.workEnd(), 17 * 60 + 30);
    settings.setWorkEnd(8 * 60);
    QCOMPARE(settings.workStart(), 7 * 60 + 30);
    settings.setWorkEnd(30 * 60);
    QCOMPARE(settings.workEnd(), 24 * 60);
    // Not shaded until asked for.
    QVERIFY(!settings.showWorkHours());
    QSignalSpy changed(&settings, &Settings::workHoursChanged);
    settings.setShowWorkHours(true);
    QCOMPARE(changed.size(), 1);
    QVERIFY(Settings(path()).showWorkHours());
}

void TestSettings::newCalendarsStartAsTheirProviderShowsThem()
{
    const auto calendar = [](const char *id, bool shown) {
        CalendarInfo info;
        info.id = QString::fromLatin1(id);
        info.enabled = shown;
        return info;
    };
    {
        Settings settings(path());
        QSignalSpy hidden(&settings, &Settings::hiddenCalendarsChanged);
        settings.seedCalendars({calendar("work", true), calendar("birthdays", false)});
        QCOMPARE(settings.hiddenCalendars(), QStringList{u"birthdays"_s});
        QCOMPARE(hidden.size(), 1);
        // Shown in Callie, it stays shown though Google still hides it.
        settings.setCalendarVisible(u"birthdays"_s, true);
        settings.seedCalendars({calendar("work", true), calendar("birthdays", false)});
        QVERIFY(settings.hiddenCalendars().isEmpty());
    }
    // Remembered across runs; a calendar new since starts as Google has it.
    Settings settings(path());
    settings.seedCalendars({calendar("birthdays", false), calendar("holidays", false)});
    QCOMPARE(settings.hiddenCalendars(), QStringList{u"holidays"_s});
}

void TestSettings::accountPhotosAreKept()
{
    {
        Settings settings(path());
        QSignalSpy changed(&settings, &Settings::accountPhotosChanged);
        settings.setAccountPhoto(u"me@example.com"_s, QUrl(u"https://lh3/me"_s));
        settings.setAccountPhoto(u"me@example.com"_s, QUrl(u"https://lh3/me"_s));
        QCOMPARE(changed.size(), 1);
    }
    Settings settings(path());
    QCOMPARE(settings.accountPhotos().value(u"me@example.com"_s).toUrl(),
             QUrl(u"https://lh3/me"_s));
    // No photo of its own any more: back to the letter.
    settings.setAccountPhoto(u"me@example.com"_s, {});
    QVERIFY(settings.accountPhotos().isEmpty());
}

void TestSettings::keyboardSettingsKeepToWhatWorks()
{
    {
        Settings settings(path());
        QVERIFY(!settings.viMode());
        QCOMPARE(settings.leaderKey(), u","_s);
        QCOMPARE(settings.leaderTimeout(), 5000);
        QSignalSpy changed(&settings, &Settings::keyboardChanged);
        settings.setViMode(true);
        settings.setLeaderKey(u"x"_s);
        QCOMPARE(settings.leaderKey(), u","_s);
        settings.setLeaderKey(u" "_s);
        settings.setLeaderTimeout(-500);
        QCOMPARE(settings.leaderTimeout(), 0);
        QCOMPARE(changed.size(), 3);
    }
    Settings again(path());
    QVERIFY(again.viMode());
    QCOMPARE(again.leaderKey(), u" "_s);
    QCOMPARE(again.leaderTimeout(), 0);
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
    settings.setTheme(u"/somewhere/mine.toml"_s);
    settings.setDefaultCalendar(u"google/me/work"_s);
    settings.setCalendarName(u"google/me/work"_s, u"Job"_s);
    settings.setAccountName(u"me@example.com"_s, u"Work"_s);
    settings.setAccountPhoto(u"me@example.com"_s, QUrl(u"https://lh3/me"_s));
    QSignalSpy widen(&settings, &Settings::widenTodayChanged);
    QSignalSpy photos(&settings, &Settings::accountPhotosChanged);
    QSignalSpy looks(&settings, &Settings::calendarLooksChanged);
    QSignalSpy accounts(&settings, &Settings::accountNamesChanged);
    QSignalSpy calendar(&settings, &Settings::defaultCalendarChanged);
    QSignalSpy declined(&settings, &Settings::showDeclinedChanged);

    settings.reset();

    QVERIFY(!settings.widenToday());
    QVERIFY(settings.timeZoneId().isEmpty());
    QVERIFY(settings.theme().isEmpty());
    QCOMPARE(widen.size(), 1);
    QCOMPARE(declined.size(), 0);
    QVERIFY(settings.defaultCalendar().isEmpty());
    QCOMPARE(calendar.size(), 1);
    QVERIFY(settings.calendarLooks().isEmpty());
    QCOMPARE(settings.accountName(u"me@example.com"_s), u"me@example.com"_s);
    QCOMPARE(looks.size(), 1);
    QCOMPARE(accounts.size(), 1);
    // Account photos come from Google rather than a choice, so they stay.
    QCOMPARE(photos.size(), 0);
    QCOMPARE(Settings(path()).accountPhotos().value(u"me@example.com"_s).toUrl(),
             QUrl(u"https://lh3/me"_s));
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

void TestSettings::gridTimesAreWallClock()
{
    // Berlin skips 02:00 to 03:00 on 29 March 2026: 13:00 is still 13:00.
    const Times times(QTimeZone("Europe/Berlin"), true);
    const QDateTime day = QDate(2026, 3, 29).startOfDay();
    QCOMPARE(times.at(day, 13 * 60),
             QDateTime(QDate(2026, 3, 29), QTime(13, 0), QTimeZone("Europe/Berlin")));
    QCOMPARE(times.at(day, 24 * 60),
             QDateTime(QDate(2026, 3, 30), QTime(0, 0), QTimeZone("Europe/Berlin")));
}

void TestSettings::shiftsKeepTheWallClock()
{
    const QTimeZone berlin("Europe/Berlin");
    const Times times(berlin, true);
    // A day later across the change to summer time is still 10:00, not 11:00.
    QCOMPARE(times.shifted(QDateTime(QDate(2026, 3, 28), QTime(10, 0), berlin), 1, 0),
             QDateTime(QDate(2026, 3, 29), QTime(10, 0), berlin));
    // Earlier than midnight wraps into the day before.
    QCOMPARE(times.shifted(QDateTime(QDate(2026, 3, 28), QTime(0, 30), berlin), 0, -60),
             QDateTime(QDate(2026, 3, 27), QTime(23, 30), berlin));
    QCOMPARE(times.shifted(QDateTime(QDate(2026, 3, 28), QTime(23, 30), berlin), 0, 90),
             QDateTime(QDate(2026, 3, 29), QTime(1, 0), berlin));
    // A drag stays on its day: no earlier than midnight, no later than the last slot.
    const QDateTime evening(QDate(2026, 3, 28), QTime(22, 0), berlin);
    QCOMPARE(times.shiftWithinDay(evening, 60, 23 * 60 + 45), 60);
    QCOMPARE(times.shiftWithinDay(evening, 180, 23 * 60 + 45), 105);
    QCOMPARE(times.shiftWithinDay(evening, -24 * 60, 23 * 60 + 45), -22 * 60);
}

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
