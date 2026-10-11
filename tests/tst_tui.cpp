#include "Palette.h"
#include "Tui.h"
#include "TuiState.h"

#include "callie/SampleSource.h"
#include "callie/Settings.h"

#include <QLocale>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace callie::cli;
using namespace Qt::StringLiterals;

namespace {

const QDateTime kNow(QDate(2026, 10, 7), QTime(13, 40), QTimeZone::UTC);

} // namespace

class TestTui : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates)); }
    void init();
    void startsOnTodayAtWhatIsNext();
    void keysMoveAroundAsInTheApp();
    void viewsCoverTheirDays();
    void hiddenCalendarsStayHidden();
    void drawsInTheTheme();

private:
    [[nodiscard]] QStringList summaries() const;
    [[nodiscard]] QString selected() const;

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<SampleSource> m_source;
    std::unique_ptr<TuiState> m_state;
};

void TestTui::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_settings = std::make_unique<Settings>(m_dir->filePath(u"settings.ini"_s));
    m_settings->setTimeZoneId(u"UTC"_s);
    m_source = std::make_unique<SampleSource>();
    m_state = std::make_unique<TuiState>(*m_source, *m_settings, [] { return kNow; });
}

QStringList TestTui::summaries() const
{
    QStringList result;
    for (const Event &e : m_state->events())
        result.append(e.summary);
    return result;
}

QString TestTui::selected() const
{
    return m_state->selected() < 0 ? QString() : m_state->events().at(m_state->selected()).summary;
}

void TestTui::startsOnTodayAtWhatIsNext()
{
    QCOMPARE(m_state->view(), TuiState::View::Day);
    QCOMPARE(m_state->day(), QDate(2026, 10, 7));
    QVERIFY(summaries().contains(u"Dentist"_s));
    // What already ended today is passed over.
    const Event next = m_state->events().at(m_state->selected());
    QVERIFY(next.end > kNow);
    for (int i = 0; i < m_state->selected(); ++i)
        QVERIFY(m_state->events().at(i).end <= kNow || m_state->events().at(i).allDay);
}

void TestTui::keysMoveAroundAsInTheApp()
{
    QVERIFY(m_state->events().size() > 2);
    m_state->press(u"g"_s);
    m_state->press(u"g"_s);
    QCOMPARE(m_state->selected(), 0);
    QVERIFY(m_state->press(u"j"_s));
    QCOMPARE(m_state->selected(), 1);
    QVERIFY(m_state->press(u"k"_s));
    QVERIFY(m_state->press(u"Up"_s));
    QCOMPARE(m_state->selected(), 0);
    m_state->press(u"G"_s);
    QCOMPARE(m_state->selected(), int(m_state->events().size()) - 1);
    m_state->press(u"g"_s);
    QCOMPARE(m_state->pending(), u"g"_s);
    m_state->press(u"g"_s);
    QCOMPARE(m_state->selected(), 0);

    m_state->press(u"l"_s);
    QCOMPARE(m_state->day(), QDate(2026, 10, 8));
    m_state->press(u"Left"_s);
    m_state->press(u"h"_s);
    QCOMPARE(m_state->day(), QDate(2026, 10, 6));
    m_state->press(u"t"_s);
    QCOMPARE(m_state->day(), QDate(2026, 10, 7));

    // Keys it does not know are left alone.
    QVERIFY(!m_state->press(u"z"_s));
    m_state->press(u"?"_s);
    QVERIFY(m_state->helpShown());
    QVERIFY(!m_state->press(u"j"_s));
    m_state->press(u"Escape"_s);
    QVERIFY(!m_state->helpShown());
    m_state->press(u"q"_s);
    QVERIFY(m_state->quitRequested());
}

void TestTui::viewsCoverTheirDays()
{
    // The leader, then a view's letter, as in the app's vi mode.
    m_state->press(m_settings->leaderKey());
    QCOMPARE(m_state->pending(), u"leader"_s);
    m_state->press(u"w"_s);
    QCOMPARE(m_state->view(), TuiState::View::Week);
    QCOMPARE(m_state->rangeDays(), 7);
    QCOMPARE(m_state->rangeStart().dayOfWeek(), m_settings->firstDayOfWeek());
    QVERIFY(m_state->rangeStart() <= QDate(2026, 10, 7));
    QVERIFY(summaries().count(u"Standup"_s) >= 3);
    m_state->press(u"l"_s);
    QCOMPARE(m_state->day(), QDate(2026, 10, 14));

    m_state->press(u"Tab"_s);
    QCOMPARE(m_state->view(), TuiState::View::Agenda);
    QCOMPARE(m_state->rangeDays(), 14);
    QCOMPARE(m_state->rangeStart(), m_state->day());
    // In order: by day, then all-day first, then by start.
    for (int i = 1; i < m_state->events().size(); ++i) {
        const Event &a = m_state->events().at(i - 1), &b = m_state->events().at(i);
        QVERIFY(m_state->dayOf(a) < m_state->dayOf(b) ||
                (m_state->dayOf(a) == m_state->dayOf(b) &&
                 (a.allDay > b.allDay || (a.allDay == b.allDay && a.start <= b.start))));
    }
}

void TestTui::hiddenCalendarsStayHidden()
{
    const QString calendar = m_state->events().first().calendarId;
    m_settings->setCalendarVisible(calendar, false);
    m_state->reload();
    for (const Event &e : m_state->events())
        QVERIFY(e.calendarId != calendar);
}

void TestTui::drawsInTheTheme()
{
    const Palette palette = Palette::forTheme({});
    QString drawn = renderTui(*m_state, *m_settings, palette, 110, 30);
    QVERIFY2(drawn.contains(u"Today, Wednesday, October 7"_s), qPrintable(drawn));
    QVERIFY(drawn.contains(u"Dentist"_s));
    QVERIFY(drawn.contains(u"Day"_s) && drawn.contains(u"Week"_s) && drawn.contains(u"Agenda"_s));
    QVERIFY(!m_state->syncStatus().isEmpty());
    QVERIFY(drawn.contains(m_state->syncStatus()));
    QVERIFY(drawn.contains(u"▌"_s));

    m_state->press(u"Tab"_s);
    drawn = renderTui(*m_state, *m_settings, palette, 110, 30);
    QVERIFY2(drawn.contains(u"Wed 7"_s), qPrintable(drawn));
    m_state->press(u"?"_s);
    drawn = renderTui(*m_state, *m_settings, palette, 110, 30);
    QVERIFY(drawn.contains(u"Keys"_s));
    QVERIFY(drawn.contains(u"Agenda view"_s));
}

QTEST_GUILESS_MAIN(TestTui)
#include "tst_tui.moc"
