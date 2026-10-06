#include "Clock.h"
#include "Composer.h"
#include "EventModelForeign.h"

#include "callie/SampleSource.h"
#include "callie/Settings.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestComposer : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void typingFillsTheDraft();
    void submitCreatesInTheChosenCalendar();
    void untitledIsNotReady();
    void hiddenCalendarsAreNotOffered();

private:
    QTemporaryDir m_dir;
    std::unique_ptr<Settings> m_settings;
};

void TestComposer::initTestCase()
{
    // Never the user's own settings file.
    m_settings = std::make_unique<Settings>(m_dir.filePath(u"settings.ini"_s));
    m_settings->setTimeZoneId(u"Europe/Berlin"_s);
    SettingsForeign::s_instance = m_settings.get();
    Clock::instance()->freeze(
        QDateTime(QDate(2026, 10, 7), QTime(13, 40), QTimeZone("Europe/Berlin")));
}

void TestComposer::typingFillsTheDraft()
{
    SampleSource source;
    Composer composer;
    composer.setSource(&source);
    QSignalSpy draft(&composer, &Composer::draftChanged);

    composer.setText(u"Pottery friday 6-8pm at Clay Studio"_s);

    QVERIFY(!draft.isEmpty());
    QCOMPARE(composer.summary(), u"Pottery"_s);
    QCOMPARE(composer.location(), u"Clay Studio"_s);
    QCOMPARE(composer.start().toTimeZone(QTimeZone("Europe/Berlin")).time(), QTime(18, 0));
    QCOMPARE(composer.calendarId(), source.calendars().first().id);
    QVERIFY(composer.ready());
}

void TestComposer::submitCreatesInTheChosenCalendar()
{
    SampleSource source;
    Composer composer;
    composer.setSource(&source);
    composer.setText(u"Pottery friday 6-8pm"_s);
    const QString personal = source.calendars().last().id;
    composer.setCalendarId(personal);
    QSignalSpy created(&composer, &Composer::created);

    composer.submit();

    QCOMPARE(created.size(), 1);
    QVERIFY(composer.error().isEmpty());
    QVERIFY(composer.text().isEmpty());
    // Remembered for next time.
    QCOMPARE(m_settings->newEventCalendar(), personal);
    const QDateTime friday(QDate(2026, 10, 9), QTime(0, 0), QTimeZone("Europe/Berlin"));
    const QList<Event> events = source.eventsBetween(friday, friday.addDays(1), friday.timeZone());
    const auto made = std::find_if(events.cbegin(), events.cend(),
                                   [](const Event &e) { return e.summary == u"Pottery"; });
    QVERIFY(made != events.cend());
    QCOMPARE(made->calendarId, personal);

    // A new composer starts in the calendar used last.
    Composer next;
    next.setSource(&source);
    QCOMPARE(next.calendarId(), personal);
}

void TestComposer::untitledIsNotReady()
{
    SampleSource source;
    Composer composer;
    composer.setSource(&source);
    composer.setText(u"friday 3pm"_s);
    QVERIFY(composer.summary().isEmpty());
    QVERIFY(!composer.ready());
    QSignalSpy created(&composer, &Composer::created);
    composer.submit();
    QVERIFY(created.isEmpty());
}

void TestComposer::hiddenCalendarsAreNotOffered()
{
    SampleSource source;
    Composer composer;
    composer.setSource(&source);
    QCOMPARE(composer.calendars().size(), source.calendars().size());

    const QString work = source.calendars().first().id;
    m_settings->setCalendarVisible(work, false);
    QCOMPARE(composer.calendars().size(), source.calendars().size() - 1);
    QVERIFY(composer.calendarId() != work);
    m_settings->setCalendarVisible(work, true);
}

QTEST_GUILESS_MAIN(TestComposer)
#include "tst_composer.moc"
