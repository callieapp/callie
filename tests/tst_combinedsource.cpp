#include "callie/CombinedSource.h"

#include <QPromise>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace callie;
using namespace Qt::StringLiterals;

class TestCombinedSource : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void readsEverySource();
    void loadsWaitForEverySource();
    void changesGoToTheCalendarsSource();
    void statusSpeaksForAll();
    void newsComesFromTheSourcesSet();
};

namespace {

/// One calendar with one event, which records the changes asked of it.
class OneCalendar : public CalendarSource
{
public:
    OneCalendar(const QString &calendarId, const QString &summary) : m_calendarId(calendarId)
    {
        m_event.uid = summary;
        m_event.summary = summary;
        m_event.calendarId = calendarId;
        m_event.start = QDateTime(QDate(2026, 10, 5), QTime(9, 0), QTimeZone::UTC);
        m_event.end = m_event.start.addSecs(3600);
    }

    QString sourceId() const override { return m_calendarId; }
    QList<CalendarInfo> calendars() const override
    {
        CalendarInfo calendar;
        calendar.id = m_calendarId;
        calendar.displayName = m_calendarId;
        return {calendar};
    }
    QList<Event> eventsBetween(const QDateTime &, const QDateTime &,
                               const QTimeZone &) const override
    {
        return {m_event};
    }
    QFuture<SourceSnapshot> load(const QDateTime &from, const QDateTime &to,
                                 const QTimeZone &tz) const override
    {
        if (!slow)
            return CalendarSource::load(from, to, tz);
        pending = std::make_shared<QPromise<SourceSnapshot>>();
        pending->start();
        return pending->future();
    }
    void answer()
    {
        pending->addResult(SourceSnapshot{{m_event}, calendars()});
        pending->finish();
    }
    void refresh() override { ++refreshes; }
    void createEvent(const EventDraft &draft, Created done) override
    {
        calls << u"create "_s + draft.summary;
        done({});
    }
    void deleteEvent(const Event &event, bool, Created done) override
    {
        calls << u"delete "_s + event.summary;
        done({});
    }
    bool syncing() const override { return busy; }
    QDateTime lastSynced() const override { return synced; }
    QString lastError() const override { return error; }

    Event item() const { return m_event; }

    bool slow = false;
    mutable std::shared_ptr<QPromise<SourceSnapshot>> pending;
    QStringList calls;
    int refreshes = 0;
    bool busy = false;
    QDateTime synced;
    QString error;

private:
    QString m_calendarId;
    Event m_event;
};

} // namespace

void TestCombinedSource::readsEverySource()
{
    OneCalendar google(u"me@example.com/work"_s, u"Standup"_s);
    OneCalendar local(u"local:home"_s, u"Dentist"_s);
    CombinedSource combined;
    combined.setSources({&google, &local});

    QCOMPARE(combined.calendars().size(), 2);
    QCOMPARE(combined.calendars().last().id, u"local:home"_s);
    QCOMPARE(combined.eventsBetween({}, {}, QTimeZone::UTC).size(), 2);
    combined.refresh();
    QCOMPARE(google.refreshes, 1);
    QCOMPARE(local.refreshes, 1);

    // A source's news is the combination's.
    QSignalSpy changed(&combined, &CalendarSource::changed);
    Q_EMIT local.changed();
    QCOMPARE(changed.size(), 1);
}

void TestCombinedSource::loadsWaitForEverySource()
{
    OneCalendar google(u"me@example.com/work"_s, u"Standup"_s);
    OneCalendar local(u"local:home"_s, u"Dentist"_s);
    local.slow = true;
    CombinedSource combined;
    combined.setSources({&google, &local});

    QFuture<SourceSnapshot> load = combined.load({}, {}, QTimeZone::UTC);
    QCoreApplication::processEvents();
    QVERIFY(!load.isFinished());
    local.answer();
    load.waitForFinished();
    const SourceSnapshot snapshot = load.result();
    QCOMPARE(snapshot.events.size(), 2);
    QCOMPARE(snapshot.calendars.size(), 2);
    QCOMPARE(snapshot.events.first().summary, u"Standup"_s);
}

void TestCombinedSource::changesGoToTheCalendarsSource()
{
    OneCalendar google(u"me@example.com/work"_s, u"Standup"_s);
    OneCalendar local(u"local:home"_s, u"Dentist"_s);
    CombinedSource combined;
    combined.setSources({&google, &local});

    EventDraft draft;
    draft.calendarId = u"local:home"_s;
    draft.summary = u"Lunch"_s;
    QString error = u"unset"_s;
    combined.createEvent(draft, [&error](const Outcome &outcome) { error = outcome.error; });
    QVERIFY(error.isEmpty());
    combined.deleteEvent(google.item(), false, [](const Outcome &) {});
    QCOMPARE(local.calls, QStringList{u"create Lunch"_s});
    QCOMPARE(google.calls, QStringList{u"delete Standup"_s});

    // A calendar no source holds, as once its account is gone.
    draft.calendarId = u"gone"_s;
    combined.createEvent(draft, [&error](const Outcome &outcome) { error = outcome.error; });
    QVERIFY(!error.isEmpty());
}

void TestCombinedSource::statusSpeaksForAll()
{
    OneCalendar google(u"me@example.com/work"_s, u"Standup"_s);
    OneCalendar local(u"local:home"_s, u"Dentist"_s);
    CombinedSource combined;
    combined.setSources({&google, &local});

    // A source that never syncs does not count against the others.
    google.synced = QDateTime(QDate(2026, 10, 5), QTime(9, 0));
    QCOMPARE(combined.lastSynced(), google.synced);
    local.synced = QDateTime(QDate(2026, 10, 5), QTime(8, 0));
    QCOMPARE(combined.lastSynced(), local.synced);

    QVERIFY(!combined.syncing());
    local.busy = true;
    QVERIFY(combined.syncing());
    local.error = u"offline"_s;
    QCOMPARE(combined.lastError(), u"offline"_s);
}

void TestCombinedSource::newsComesFromTheSourcesSet()
{
    OneCalendar google(u"me@example.com/work"_s, u"Standup"_s);
    OneCalendar local(u"local:home"_s, u"Dentist"_s);
    CombinedSource combined;
    combined.setSources({&google});
    QSignalSpy errors(&combined, &CalendarSource::errorOccurred);
    QSignalSpy made(&combined, &CalendarSource::changeMade);
    Q_EMIT google.errorOccurred(u"offline"_s);
    Q_EMIT google.changeMade(u"1"_s, u"Moved Standup"_s);
    QCOMPARE(errors.size(), 1);
    QCOMPARE(made.size(), 1);
    QCOMPARE(made.first().at(1).toString(), u"Moved Standup"_s);

    // Once replaced, a source is no longer heard.
    combined.setSources({&local});
    QCOMPARE(combined.calendars().size(), 1);
    Q_EMIT google.errorOccurred(u"offline"_s);
    Q_EMIT google.changed();
    QCOMPARE(errors.size(), 1);
    Q_EMIT local.errorOccurred(u"disk full"_s);
    QCOMPARE(errors.size(), 2);
}

QTEST_MAIN(TestCombinedSource)
#include "tst_combinedsource.moc"
