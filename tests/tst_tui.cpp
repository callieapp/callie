#include "Palette.h"
#include "Tui.h"
#include "TuiState.h"

#include "callie/QueuedSource.h"
#include "callie/SampleSource.h"
#include "callie/Settings.h"

#include <QLocale>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace callie::cli;
using namespace Qt::StringLiterals;

namespace {

const QDateTime kNow(QDate(2026, 10, 7), QTime(13, 40), QTimeZone::UTC);

/// A few fixed events.
class FixedSource : public CalendarSource
{
public:
    explicit FixedSource(QList<Event> events) : m_events(std::move(events)) {}

    QString sourceId() const override { return u"fixed"_s; }
    QList<CalendarInfo> calendars() const override { return {}; }
    void refresh() override {}
    QList<Event> eventsBetween(const QDateTime &from, const QDateTime &to,
                               const QTimeZone &) const override
    {
        QList<Event> out;
        for (const Event &e : m_events) {
            if (e.end > from && e.start < to)
                out.append(e);
        }
        return out;
    }

private:
    QList<Event> m_events;
};

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
    void daysAreTheSettingsZones();
    void syncStatusRedraws();
    void drawsInTheTheme();
    void detailsAnswerAndTakeBack();
    void decliningAHiddenOneClosesItsDetails();
    void deletingCanBeTakenBack();
    void newEventsAsCallieAddReadsThem();
    void searchAndInvitationsJumpToTheDay();
    void foundAsTheCliListsIt();
    void foundKeepsItsCalendar();
    void refusedChangesAreShown();
    void calendarsShowAndHide();
    void callsOpen();
    void onlyWebCallsOpen();

private:
    [[nodiscard]] QStringList summaries() const;
    [[nodiscard]] QString selected() const;

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<SampleSource> m_source;
    std::unique_ptr<QueuedSource> m_queue;
    std::unique_ptr<TuiState> m_state;
    /// Selects the first event called `summary` in what is shown.
    bool select(const QString &summary);
};

void TestTui::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_settings = std::make_unique<Settings>(m_dir->filePath(u"settings.ini"_s));
    m_settings->setTimeZoneId(u"UTC"_s);
    m_source = std::make_unique<SampleSource>();
    m_source->setNow([] { return kNow; });
    // As the TUI runs it: changes held a moment, so they can be taken back.
    m_queue = std::make_unique<QueuedSource>(*m_source, QString());
    m_queue->setNow([] { return kNow; });
    m_state = std::make_unique<TuiState>(*m_queue, *m_settings, [] { return kNow; });
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

void TestTui::daysAreTheSettingsZones()
{
    // Los Angeles leaves daylight time early on November 1, so that day has
    // 25 hours. 07:30 UTC is 00:30 there, still the start of the day.
    m_settings->setTimeZoneId(u"America/Los_Angeles"_s);
    const QTimeZone la("America/Los_Angeles");
    const auto at = [&](int day, int hour, int minute) {
        return QDateTime(QDate(2026, 11, day), QTime(hour, minute), la);
    };
    const auto event = [](const QString &summary, const QDateTime &start, const QDateTime &end) {
        Event e;
        e.eventId = summary;
        e.summary = summary;
        e.start = start;
        e.end = end;
        return e;
    };
    FixedSource source({
        event(u"Late"_s, QDateTime(QDate(2026, 10, 31), QTime(23, 30), la), at(1, 0, 15)),
        event(u"Brunch"_s, at(1, 11, 0), at(1, 12, 0)),
        event(u"Night"_s, at(1, 23, 30), at(1, 23, 50)),
        event(u"Tomorrow"_s, at(2, 0, 30), at(2, 1, 0)),
    });
    const QDateTime now(QDate(2026, 11, 1), QTime(7, 30), QTimeZone::UTC);
    TuiState state(source, *m_settings, [now] { return now; });

    QCOMPARE(state.today(), QDate(2026, 11, 1));
    QStringList shown;
    for (const Event &e : state.events())
        shown.append(e.summary);
    QCOMPARE(shown, (QStringList{u"Late"_s, u"Brunch"_s, u"Night"_s}));
    QCOMPARE(state.dayOf(state.events().first()), QDate(2026, 11, 1));

    state.press(u"Tab"_s);
    QCOMPARE(state.view(), TuiState::View::Week);
    for (const Event &e : state.events()) {
        const QDate expected = e.summary == u"Tomorrow"_s ? QDate(2026, 11, 2)
                               : e.summary == u"Late"_s && state.rangeStart() < QDate(2026, 11, 1)
                                   ? QDate(2026, 10, 31)
                                   : QDate(2026, 11, 1);
        QCOMPARE(state.dayOf(e), expected);
    }
}

void TestTui::syncStatusRedraws()
{
    QSignalSpy redraws(m_state.get(), &TuiState::changed);
    Q_EMIT m_source->statusChanged();
    QCOMPARE(redraws.count(), 1);
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

bool TestTui::select(const QString &summary)
{
    m_state->press(u"g"_s);
    m_state->press(u"g"_s);
    for (int i = 0; i < m_state->events().size(); ++i) {
        if (m_state->events().at(m_state->selected()).summary == summary)
            return true;
        m_state->press(u"j"_s);
    }
    return false;
}

void TestTui::detailsAnswerAndTakeBack()
{
    // The coming week's Berlin sync waits for an answer.
    m_state->press(m_settings->leaderKey());
    m_state->press(u"a"_s);
    QVERIFY(select(u"Berlin sync"_s));
    m_state->press(u"Enter"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Details);
    QString drawn = renderTui(*m_state, *m_settings, Palette::forTheme({}), 110, 30);
    QVERIFY2(drawn.contains(u"Not answered"_s), qPrintable(drawn));
    QVERIFY(drawn.contains(u"y yes"_s));

    m_state->press(u"y"_s);
    QCOMPARE(m_state->current()->responseStatus, u"accepted"_s);
    QVERIFY2(m_state->message().contains(u"u takes it back"_s), qPrintable(m_state->message()));
    // Taken back from the details, as the message says.
    QVERIFY(m_state->press(u"u"_s));
    QCOMPARE(m_state->mode(), TuiState::Mode::Details);
    QCOMPARE(m_state->current()->responseStatus, u"needsAction"_s);
    m_state->press(u"Escape"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Browse);
    // Only an invitation can be answered.
    QVERIFY(select(u"Dentist"_s));
    m_state->press(u"Enter"_s);
    m_state->press(u"y"_s);
    QVERIFY(m_state->messageIsError());
}

void TestTui::decliningAHiddenOneClosesItsDetails()
{
    m_settings->setShowDeclined(false);
    m_state->press(m_settings->leaderKey());
    m_state->press(u"a"_s);
    QVERIFY(select(u"Berlin sync"_s));
    const QString declined = m_state->current()->eventId;
    const int shown = int(m_state->events().size());
    m_state->press(u"Enter"_s);
    m_state->press(u"n"_s);
    // Declined, so no longer shown, and the details do not move on to another
    // event that a following x would delete.
    for (const Event &e : m_state->events())
        QVERIFY(e.eventId != declined);
    QCOMPARE(m_state->mode(), TuiState::Mode::Browse);
    QVERIFY(!m_state->press(u"x"_s));
    QCOMPARE(int(m_state->events().size()), shown - 1);
}

void TestTui::deletingCanBeTakenBack()
{
    QVERIFY(select(u"Dentist"_s));
    m_state->press(u"Enter"_s);
    m_state->press(u"x"_s);
    QVERIFY(!summaries().contains(u"Dentist"_s));
    QCOMPARE(m_state->mode(), TuiState::Mode::Browse);
    m_state->press(u"u"_s);
    QVERIFY(summaries().contains(u"Dentist"_s));
    // Nothing more to take back.
    m_state->press(u"u"_s);
    QVERIFY(m_state->messageIsError());
}

void TestTui::newEventsAsCallieAddReadsThem()
{
    m_state->press(u"n"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Typing);
    for (const QChar c : u"Pottery today 6-8pm"_s)
        m_state->press(QString(c));
    m_state->press(u"Backspace"_s);
    m_state->press(u"m"_s);
    QCOMPARE(m_state->input(), u"Pottery today 6-8pm"_s);
    const QString drawn = renderTui(*m_state, *m_settings, Palette::forTheme({}), 110, 30);
    QVERIFY(drawn.contains(u"New event: Pottery today 6-8pm"_s));
    m_state->press(u"Enter"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Browse);
    QVERIFY2(summaries().contains(u"Pottery"_s), qPrintable(summaries().join(u", "_s)));
    QVERIFY(!m_state->messageIsError());

    // What callie add cannot read, such as a time with no title, it says so.
    m_state->press(u"n"_s);
    for (const QChar c : u"tomorrow 3pm"_s)
        m_state->press(QString(c));
    m_state->press(u"Enter"_s);
    QVERIFY(m_state->messageIsError());
    // Escape gives up on the line.
    m_state->press(u"n"_s);
    m_state->press(u"x"_s);
    m_state->press(u"Escape"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Browse);
}

void TestTui::searchAndInvitationsJumpToTheDay()
{
    m_state->press(u"/"_s);
    for (const QChar c : u"climbing"_s)
        m_state->press(QString(c));
    m_state->press(u"Enter"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Found);
    QVERIFY(!m_state->found().isEmpty());
    const Event climbing = m_state->found().at(m_state->foundSelected());
    QCOMPARE(climbing.summary, u"Climbing"_s);
    m_state->press(u"Enter"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Browse);
    QCOMPARE(m_state->view(), TuiState::View::Day);
    QCOMPARE(m_state->day(), climbing.start.date());
    QCOMPARE(m_state->current()->eventId, climbing.eventId);

    // A repeating event is found once, at its next occurrence.
    m_state->press(u"/"_s);
    for (const QChar c : u"standup"_s)
        m_state->press(QString(c));
    m_state->press(u"Enter"_s);
    QCOMPARE(m_state->found().size(), 1);
    QVERIFY(m_state->found().first().end > kNow);
    m_state->press(u"Escape"_s);

    m_state->press(u"i"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Found);
    for (const Event &e : m_state->found())
        QCOMPARE(e.responseStatus, u"needsAction"_s);
    QVERIFY(!m_state->found().isEmpty());
    m_state->press(u"Escape"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Browse);
}

void TestTui::foundAsTheCliListsIt()
{
    // A repeating invitation, answered once, and where the user works, which
    // labels a day rather than being an event.
    QList<Event> events;
    for (int week = 0; week < 3; ++week) {
        Event e;
        e.calendarId = u"work"_s;
        e.eventId = u"planning-%1"_s.arg(week);
        e.seriesId = u"planning"_s;
        e.uid = u"planning"_s;
        e.summary = u"Planning"_s;
        e.start = kNow.addDays(1 + 7 * week);
        e.end = e.start.addSecs(3600);
        e.responseStatus = u"needsAction"_s;
        e.canRespond = true;
        events.append(e);
    }
    Event office;
    office.calendarId = u"work"_s;
    office.eventId = office.uid = u"office"_s;
    office.summary = u"Office"_s;
    office.start = kNow.addDays(1);
    office.end = office.start.addDays(1);
    office.allDay = office.workPlace = true;
    office.responseStatus = u"needsAction"_s;
    office.canRespond = true;
    events.append(office);
    FixedSource source(events);
    TuiState state(source, *m_settings, [] { return kNow; });

    state.press(u"i"_s);
    QCOMPARE(state.found().size(), 1);
    QCOMPARE(state.found().first().eventId, u"planning-0"_s);
    state.press(u"Escape"_s);

    for (const QString &key : {u"/"_s, u"o"_s, u"f"_s, u"f"_s, u"Enter"_s})
        state.press(key);
    QVERIFY(state.found().isEmpty());
    state.press(u"Escape"_s);
    for (const QString &key : {u"/"_s, u"p"_s, u"l"_s, u"a"_s, u"n"_s, u"Enter"_s})
        state.press(key);
    QCOMPARE(state.found().size(), 1);
}

void TestTui::foundKeepsItsCalendar()
{
    // One meeting on two calendars, with the same id in each.
    QList<Event> events;
    for (const QString &calendar : {u"work"_s, u"team"_s}) {
        Event e;
        e.calendarId = calendar;
        e.eventId = e.uid = u"review"_s;
        e.summary = u"Review"_s;
        e.start = kNow.addDays(2);
        e.end = e.start.addSecs(3600);
        events.append(e);
    }
    FixedSource source(events);
    TuiState state(source, *m_settings, [] { return kNow; });
    for (int pick = 0; pick < 2; ++pick) {
        for (const QString &key : {u"/"_s, u"r"_s, u"e"_s, u"v"_s, u"Enter"_s})
            state.press(key);
        QCOMPARE(state.found().size(), 2);
        state.press(u"g"_s);
        state.press(u"g"_s);
        if (pick == 1)
            state.press(u"j"_s);
        QCOMPARE(state.foundSelected(), pick);
        const QString calendar = state.found().at(pick).calendarId;
        state.press(u"Enter"_s);
        QCOMPARE(state.current()->calendarId, calendar);
    }
}

void TestTui::refusedChangesAreShown()
{
    Q_EMIT m_source->errorOccurred(u"Delete Dentist: not allowed"_s);
    QVERIFY(m_state->messageIsError());
    QCOMPARE(m_state->message(), u"Delete Dentist: not allowed"_s);
}

void TestTui::calendarsShowAndHide()
{
    m_state->press(u"c"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Calendars);
    const CalendarInfo first = m_state->calendars().first();
    QVERIFY(m_state->calendarShown(first));
    QString drawn = renderTui(*m_state, *m_settings, Palette::forTheme({}), 110, 30);
    QVERIFY(drawn.contains(u"Calendars"_s) && drawn.contains(first.displayName));
    m_state->press(u" "_s);
    QVERIFY(!m_state->calendarShown(first));
    for (const Event &e : m_state->events())
        QVERIFY(e.calendarId != first.id);
    m_state->press(u" "_s);
    QVERIFY(m_state->calendarShown(first));
    m_state->press(u"Escape"_s);
    QCOMPARE(m_state->mode(), TuiState::Mode::Browse);
}

void TestTui::callsOpen()
{
    QSignalSpy opened(m_state.get(), &TuiState::openRequested);
    m_state->press(m_settings->leaderKey());
    m_state->press(u"a"_s);
    QVERIFY(select(u"Standup"_s));
    m_state->press(u"Enter"_s);
    m_state->press(u"o"_s);
    QCOMPARE(opened.size(), 1);
    QCOMPARE(opened.first().first().toUrl(), m_state->current()->joinUrl());
}

void TestTui::onlyWebCallsOpen()
{
    Event e;
    e.calendarId = u"work"_s;
    e.eventId = e.uid = u"call"_s;
    e.summary = u"Call"_s;
    e.start = kNow.addSecs(3600);
    e.end = e.start.addSecs(3600);
    e.conferenceUrl = QUrl(u"file:///home/sam/run.sh"_s);
    FixedSource source({e});
    TuiState state(source, *m_settings, [] { return kNow; });
    QSignalSpy opened(&state, &TuiState::openRequested);
    QCOMPARE(state.current()->summary, u"Call"_s);
    state.press(u"Enter"_s);
    state.press(u"o"_s);
    QCOMPARE(opened.size(), 0);
    QVERIFY(state.messageIsError());
}

QTEST_GUILESS_MAIN(TestTui)
#include "tst_tui.moc"
