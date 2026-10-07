#include "callie/QueuedSource.h"

#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

/// A server in miniature: holds events, records what it is asked to do, and
/// can refuse or hold the answer back.
class FakeServer : public CalendarSource
{
public:
    QList<Event> events;
    QStringList calls;
    QString refuseWith;
    /// Fail in a way worth trying again, such as a busy server.
    bool busy = false;
    /// Answers wait here until released.
    QList<std::pair<Created, QString>> held;
    bool hold = false;

    QString sourceId() const override { return u"fake"_s; }
    QList<CalendarInfo> calendars() const override
    {
        return {{u"work"_s, u"Work"_s, QColor(u"#5b8def"_s), true, true}};
    }
    QList<Event> eventsBetween(const QDateTime &from, const QDateTime &to,
                               const QTimeZone &) const override
    {
        QList<Event> result;
        for (const Event &e : events) {
            if (e.start < to && e.end > from)
                result.append(e);
        }
        return result;
    }
    void refresh() override {}

    void answer(const Created &done)
    {
        if (hold)
            held.append({done, refuseWith});
        else if (busy)
            done({u"try later"_s, true});
        else
            done(refuseWith);
    }
    void createEvent(const EventDraft &draft, Created done) override
    {
        calls << u"create "_s + draft.summary;
        if (refuseWith.isEmpty() && !hold) {
            Event e;
            e.uid = e.eventId = u"real-"_s + draft.summary;
            e.calendarId = draft.calendarId;
            e.summary = draft.summary;
            e.start = draft.start;
            e.end = draft.end;
            events.append(e);
        }
        answer(done);
    }
    void respond(const Event &event, const QString &status, bool, Created done) override
    {
        calls << u"respond "_s + event.eventId + u' ' + status;
        answer(done);
    }
    void deleteEvent(const Event &event, bool, Created done) override
    {
        calls << u"delete "_s + event.eventId;
        if (refuseWith.isEmpty())
            events.removeIf([&event](const Event &e) { return e.eventId == event.eventId; });
        answer(done);
    }
    void updateEvent(const Event &event, const EventEdit &edit, EditScope scope,
                     Created done) override
    {
        calls << u"update "_s + event.eventId + u' ' + edit.summary.value_or(QString()) +
                     (scope == EditScope::AllEvents ? u" all"_s : QString());
        answer(done);
    }
    void moveEvent(const Event &event, const QDateTime &start, const QDateTime &, bool,
                   Created done) override
    {
        calls << u"move "_s + event.eventId + u' ' + start.toString(u"HH:mm"_s);
        answer(done);
    }
};

QDateTime at(int hour, int minute = 0)
{
    return QDateTime(QDate(2026, 10, 7), QTime(hour, minute), QTimeZone::UTC);
}

Event makeEvent(const QString &id, int hour)
{
    Event e;
    e.uid = e.eventId = id;
    e.summary = id;
    e.calendarId = u"work"_s;
    e.start = at(hour);
    e.end = at(hour + 1);
    return e;
}

QStringList shown(const CalendarSource &source)
{
    QStringList list;
    for (const Event &e : source.eventsBetween(at(0), at(23), QTimeZone::UTC))
        list << e.summary + u'@' + e.start.toString(u"HH:mm"_s);
    list.sort();
    return list;
}

} // namespace

class TestQueuedSource : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void changesShowAtOnceAndAreSent();
    void offlineChangesWaitAndSurviveARestart();
    void refusedChangeIsDroppedAndReported();
    void changesCanBeTakenBack();
    void changesFoldedIntoACreationCanBeTakenBack();
    void pendingCreationTakesLaterChanges();
    void changesGoOutInOrder();
    void passingFailuresAreTriedAgain();
    void seriesMovesOnItsOwnClock();
    void allDaySeriesMovesByWholeDays();
    void editsShowAtOnceAndSurviveARestart();
    void editsFoldIntoAPendingCreation();
    void creationsKeepTheirNotes();
    void followingEditsShowFromTheOccurrenceOn();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    FakeServer m_server;
    QDateTime m_now;
    bool m_online = true;

    /// Lets the hold on every change run out, and sends what is ready.
    void release(QueuedSource &source)
    {
        m_now = m_now.addSecs(QueuedSource::kHoldSecs);
        source.flush();
    }

    std::unique_ptr<QueuedSource> make()
    {
        auto source = std::make_unique<QueuedSource>(m_server, m_dir->filePath(u"pending.json"_s));
        source->setNow([this] { return m_now; });
        source->setOnline([this] { return m_online; });
        return source;
    }
};

void TestQueuedSource::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_server.events = {makeEvent(u"standup"_s, 9), makeEvent(u"lunch"_s, 12)};
    m_server.calls.clear();
    m_server.refuseWith.clear();
    m_server.held.clear();
    m_server.hold = false;
    m_server.busy = false;
    m_now = at(8);
    m_online = true;
}

void TestQueuedSource::changesShowAtOnceAndAreSent()
{
    auto source = make();
    m_server.hold = true;
    QString error = u"unset"_s;
    source->moveEvent(makeEvent(u"standup"_s, 9), at(10), at(11), false,
                      [&error](const QString &e) { error = e; });

    // Done and shown at once, and held a moment before it goes.
    QCOMPARE(error, QString());
    QCOMPARE(shown(*source), (QStringList{u"lunch@12:00"_s, u"standup@10:00"_s}));
    QVERIFY(m_server.calls.isEmpty());
    QVERIFY(source->waitingChanges().isEmpty());
    release(*source);
    QCOMPARE(m_server.calls, QStringList{u"move standup 10:00"_s});
    QCOMPARE(source->waitingChanges(), QStringList{u"Move standup"_s});

    // The server's answer clears it from the queue.
    m_server.held.takeFirst().first({});
    QVERIFY(source->waitingChanges().isEmpty());
}

void TestQueuedSource::offlineChangesWaitAndSurviveARestart()
{
    m_online = false;
    {
        auto source = make();
        source->respond(makeEvent(u"lunch"_s, 12), u"accepted"_s, false, [](const QString &) {});
        release(*source);
        QVERIFY(m_server.calls.isEmpty());
        QCOMPARE(source->waitingChanges(), QStringList{u"Answer lunch"_s});
    }
    // Back after a restart, and sent once online.
    auto source = make();
    QCOMPARE(source->waitingChanges(), QStringList{u"Answer lunch"_s});
    const Event lunch = source->eventsBetween(at(12), at(13), QTimeZone::UTC).first();
    QCOMPARE(lunch.responseStatus, u"accepted"_s);
    m_online = true;
    source->flush();
    QCOMPARE(m_server.calls, QStringList{u"respond lunch accepted"_s});
    QVERIFY(source->waitingChanges().isEmpty());
}

void TestQueuedSource::refusedChangeIsDroppedAndReported()
{
    auto source = make();
    m_server.refuseWith = u"not allowed"_s;
    QSignalSpy failed(source.get(), &CalendarSource::errorOccurred);
    source->moveEvent(makeEvent(u"standup"_s, 9), at(10), at(11), false, [](const QString &) {});
    release(*source);

    QCOMPARE(failed.size(), 1);
    QVERIFY(source->lastError().contains(u"not allowed"_s));
    // Shown as it really is again.
    QCOMPARE(shown(*source), (QStringList{u"lunch@12:00"_s, u"standup@09:00"_s}));
    QVERIFY(source->waitingChanges().isEmpty());

    // The next change that goes through clears the complaint.
    m_server.refuseWith.clear();
    source->moveEvent(makeEvent(u"lunch"_s, 12), at(13), at(14), false, [](const QString &) {});
    release(*source);
    QVERIFY(source->lastError().isEmpty());
}

void TestQueuedSource::changesCanBeTakenBack()
{
    auto source = make();
    QSignalSpy made(source.get(), &CalendarSource::changeMade);
    source->deleteEvent(makeEvent(u"lunch"_s, 12), false, [](const QString &) {});
    QCOMPARE(shown(*source), QStringList{u"standup@09:00"_s});
    QVERIFY(m_server.calls.isEmpty());
    QCOMPARE(made.first().at(1).toString(), u"Deleted lunch"_s);

    // Taken back in time: never sent.
    QVERIFY(source->undoChange(made.first().first().toString()));
    QCOMPARE(shown(*source), (QStringList{u"lunch@12:00"_s, u"standup@09:00"_s}));

    // Left alone, it goes once the hold is over.
    source->deleteEvent(makeEvent(u"lunch"_s, 12), false, [](const QString &) {});
    release(*source);
    QCOMPARE(m_server.calls, QStringList{u"delete lunch"_s});
    // Gone out, so too late to take back.
    QVERIFY(!source->undoChange(made.last().first().toString()));
}

void TestQueuedSource::changesFoldedIntoACreationCanBeTakenBack()
{
    auto source = make();
    QSignalSpy made(source.get(), &CalendarSource::changeMade);
    EventDraft draft;
    draft.summary = u"Pottery"_s;
    draft.calendarId = u"work"_s;
    draft.start = at(18);
    draft.end = at(20);
    source->createEvent(draft, [](const QString &) {});
    const Event pottery = source->eventsBetween(at(18), at(19), QTimeZone::UTC).first();

    // A move into the creation gets its own toast, and holds the creation
    // again, so undoing it still works after the creation's own hold.
    m_now = m_now.addSecs(QueuedSource::kHoldSecs - 1);
    source->moveEvent(pottery, at(19), at(21), false, [](const QString &) {});
    m_now = m_now.addSecs(2);
    source->flush();
    QVERIFY(m_server.calls.isEmpty());
    QCOMPARE(made.size(), 2);
    QCOMPARE(made.last().at(1).toString(), u"Moved Pottery"_s);
    QVERIFY(source->undoChange(made.last().first().toString()));
    QCOMPARE(source->changes().first().draft.start, at(18));

    // Deleting it, then undoing that, brings the creation back.
    source->deleteEvent(pottery, false, [](const QString &) {});
    QVERIFY(source->changes().isEmpty());
    QVERIFY(source->undoChange(made.last().first().toString()));
    QCOMPARE(source->changes().size(), 1);

    // Once the creation has gone out, a move folded into it earlier stays.
    source->moveEvent(pottery, at(19), at(21), false, [](const QString &) {});
    const QString moved = made.last().first().toString();
    release(*source);
    QVERIFY(source->changes().isEmpty());
    QVERIFY(!source->undoChange(moved));
    QCOMPARE(m_server.calls, QStringList{u"create Pottery"_s});
}

void TestQueuedSource::pendingCreationTakesLaterChanges()
{
    m_online = false;
    auto source = make();
    EventDraft draft;
    draft.summary = u"Pottery"_s;
    draft.calendarId = u"work"_s;
    draft.start = at(18);
    draft.end = at(20);
    source->createEvent(draft, [](const QString &) {});
    const Event made = source->eventsBetween(at(18), at(19), QTimeZone::UTC).first();
    QCOMPARE(made.summary, u"Pottery"_s);
    // Given an id in Google's alphabet before it goes out.
    QVERIFY(QRegularExpression(u"^[0-9a-v]{32}$"_s)
                .match(source->changes().first().draft.id)
                .hasMatch());
    QCOMPARE(made.color, QColor(u"#5b8def"_s));

    // Moving it changes what will be created; deleting it creates nothing.
    source->moveEvent(made, at(19), at(21), false, [](const QString &) {});
    QCOMPARE(source->changes().size(), 1);
    QCOMPARE(source->changes().first().draft.start, at(19));
    source->deleteEvent(made, false, [](const QString &) {});
    QVERIFY(source->changes().isEmpty());
    m_online = true;
    release(*source);
    QVERIFY(m_server.calls.isEmpty());
}

void TestQueuedSource::changesGoOutInOrder()
{
    auto source = make();
    m_server.hold = true;
    source->moveEvent(makeEvent(u"standup"_s, 9), at(10), at(11), false, [](const QString &) {});
    source->moveEvent(makeEvent(u"lunch"_s, 12), at(13), at(14), false, [](const QString &) {});
    release(*source);
    // One at a time: the second waits for the first's answer.
    QCOMPARE(m_server.calls.size(), 1);
    // A change already on its way cannot be taken back.
    QVERIFY(!source->undoChange(source->changes().first().id));
    m_server.held.takeFirst().first({});
    QCOMPARE(m_server.calls.size(), 2);
    QCOMPARE(m_server.calls.last(), u"move lunch 13:00"_s);
}

void TestQueuedSource::passingFailuresAreTriedAgain()
{
    auto source = make();
    m_server.busy = true;
    source->moveEvent(makeEvent(u"standup"_s, 9), at(10), at(11), false, [](const QString &) {});
    release(*source);
    // Kept, shown moved, and no complaint, though online.
    QCOMPARE(source->waitingChanges(), QStringList{u"Move standup"_s});
    QVERIFY(source->lastError().isEmpty());
    QCOMPARE(shown(*source), (QStringList{u"lunch@12:00"_s, u"standup@10:00"_s}));

    m_server.busy = false;
    source->flush();
    QVERIFY(source->waitingChanges().isEmpty());
    QCOMPARE(m_server.calls.last(), u"move standup 10:00"_s);
}

void TestQueuedSource::seriesMovesOnItsOwnClock()
{
    // A Berlin series, shown in UTC, around the start of summer time.
    const QTimeZone berlin("Europe/Berlin");
    const auto swim = [&berlin](int day) {
        Event e;
        e.uid = u"swim"_s;
        e.eventId = u"swim_"_s + QString::number(day);
        e.seriesId = u"swim"_s;
        e.summary = u"Swim"_s;
        e.calendarId = u"work"_s;
        e.zone = u"Europe/Berlin"_s;
        e.start = QDateTime(QDate(2026, 3, day), QTime(10, 0), berlin).toUTC();
        e.end = e.start.addSecs(3600);
        return e;
    };
    m_server.events = {swim(28), swim(30)};
    // Not answered yet, so the move shows from the queue.
    m_server.hold = true;
    auto source = make();
    // Saturday's swim dragged a day on, to Sunday 10:00 in Berlin: all move.
    const Event saturday = swim(28);
    const QDateTime sunday = QDateTime(QDate(2026, 3, 29), QTime(10, 0), berlin);
    source->moveEvent(saturday, sunday, sunday.addSecs(3600), true, [](const QString &) {});

    const QDateTime from(QDate(2026, 3, 27), QTime(0, 0), QTimeZone::UTC);
    QList<QDateTime> starts;
    for (const Event &e : source->eventsBetween(from, from.addDays(7), QTimeZone::UTC))
        starts << e.start.toTimeZone(berlin);
    std::sort(starts.begin(), starts.end());
    // Still 10:00 in Berlin, though Berlin's offset changed in between.
    QCOMPARE(starts, (QList<QDateTime>{QDateTime(QDate(2026, 3, 29), QTime(10, 0), berlin),
                                       QDateTime(QDate(2026, 3, 31), QTime(10, 0), berlin)}));
}

void TestQueuedSource::editsShowAtOnceAndSurviveARestart()
{
    m_online = false;
    {
        auto source = make();
        EventEdit edit;
        edit.summary = u"Lunch with Alex"_s;
        edit.start = at(13);
        edit.end = at(14, 30);
        QSignalSpy made(source.get(), &CalendarSource::changeMade);
        source->updateEvent(makeEvent(u"lunch"_s, 12), edit, EditScope::ThisEvent,
                            [](const QString &) {});
        QCOMPARE(made.first().at(1).toString(), u"Changed lunch"_s);
        QCOMPARE(shown(*source), (QStringList{u"Lunch with Alex@13:00"_s, u"standup@09:00"_s}));
    }
    // Kept as it was, only what was set.
    auto source = make();
    const PendingChange change = source->changes().first();
    QCOMPARE(change.kind, PendingChange::Kind::Update);
    QCOMPARE(*change.edit.summary, u"Lunch with Alex"_s);
    QCOMPARE(*change.edit.end, at(14, 30));
    QVERIFY(!change.edit.location);
    m_online = true;
    release(*source);
    QCOMPARE(m_server.calls, QStringList{u"update lunch Lunch with Alex"_s});
}

void TestQueuedSource::editsFoldIntoAPendingCreation()
{
    auto source = make();
    EventDraft draft;
    draft.summary = u"Pottery"_s;
    draft.calendarId = u"work"_s;
    draft.start = at(18);
    draft.end = at(20);
    source->createEvent(draft, [](const QString &) {});
    const Event pottery = source->eventsBetween(at(18), at(19), QTimeZone::UTC).first();

    // What a creation can say is folded into it.
    EventEdit rename;
    rename.summary = u"Clay"_s;
    rename.start = at(17);
    rename.end = at(19);
    QString error = u"unset"_s;
    source->updateEvent(pottery, rename, EditScope::ThisEvent,
                        [&error](const QString &e) { error = e; });
    QVERIFY(error.isEmpty());
    QCOMPARE(source->changes().size(), 1);
    QCOMPARE(source->changes().first().draft.summary, u"Clay"_s);
    QCOMPARE(source->changes().first().draft.start, at(17));

    // Guests wait for the event to exist.
    EventEdit invite;
    invite.guests = QStringList{u"pat@example.com"_s};
    source->updateEvent(pottery, invite, EditScope::ThisEvent,
                        [&error](const QString &e) { error = e; });
    QVERIFY(error.contains(u"Wait"_s));
    QCOMPARE(source->changes().size(), 1);

    release(*source);
    QCOMPARE(m_server.calls, QStringList{u"create Clay"_s});
}

void TestQueuedSource::followingEditsShowFromTheOccurrenceOn()
{
    m_online = false;
    m_server.events.clear();
    for (int hour : {9, 11, 13}) {
        Event e = makeEvent(u"walk-"_s + QString::number(hour), hour);
        e.summary = u"Walk"_s;
        e.seriesId = u"walk"_s;
        e.recurrenceId = e.start;
        m_server.events.append(e);
    }
    auto source = make();
    EventEdit edit;
    edit.summary = u"Run"_s;
    source->updateEvent(m_server.events[1], edit, EditScope::ThisAndFollowing,
                        [](const QString &) {});
    QCOMPARE(shown(*source), (QStringList{u"Run@11:00"_s, u"Run@13:00"_s, u"Walk@09:00"_s}));
    m_online = true;
    release(*source);
    QCOMPARE(m_server.calls, QStringList{u"update walk-11 Run"_s});
}

void TestQueuedSource::allDaySeriesMovesByWholeDays()
{
    const QTimeZone york("America/New_York");
    const auto midnight = [&york](int month, int day) {
        return QDateTime(QDate(2026, month, day), QTime(0, 0), york);
    };
    // A weekly all-day series; the one on 31 October is dragged a day on,
    // across the night New York falls back, which is 25 hours long.
    QList<Event> events;
    for (const QDate day : {QDate(2026, 10, 24), QDate(2026, 10, 31)}) {
        Event e = makeEvent(u"bins-"_s + day.toString(Qt::ISODate), 0);
        e.seriesId = u"bins"_s;
        e.allDay = true;
        e.start = day.startOfDay(york);
        e.end = day.addDays(1).startOfDay(york);
        events.append(e);
    }
    PendingChange move;
    move.kind = PendingChange::Kind::Move;
    move.event = events[1];
    move.start = midnight(11, 1);
    move.end = midnight(11, 2);
    move.wholeSeries = true;
    QueuedSource::apply(events, {move}, {}, midnight(10, 1), midnight(12, 1), york);

    QCOMPARE(events[0].start, midnight(10, 25));
    QCOMPARE(events[0].end, midnight(10, 26));
    QCOMPARE(events[1].start, midnight(11, 1));
    QCOMPARE(events[1].end, midnight(11, 2));
}

void TestQueuedSource::creationsKeepTheirNotes()
{
    m_online = false;
    {
        auto source = make();
        EventDraft draft;
        draft.summary = u"Pottery"_s;
        draft.description = u"Bring an apron"_s;
        draft.calendarId = u"work"_s;
        draft.start = at(18);
        draft.end = at(20);
        source->createEvent(draft, [](const QString &) {});
        // New notes fold into the creation, as a title does.
        const Event pottery = source->eventsBetween(at(18), at(19), QTimeZone::UTC).first();
        QCOMPARE(pottery.description, u"Bring an apron"_s);
        EventEdit notes;
        notes.description = u"Bring an apron and a towel"_s;
        QString error = u"unset"_s;
        source->updateEvent(pottery, notes, EditScope::ThisEvent,
                            [&error](const QString &e) { error = e; });
        QVERIFY(error.isEmpty());
    }
    auto source = make();
    QCOMPARE(source->changes().size(), 1);
    QCOMPARE(source->changes().first().draft.description, u"Bring an apron and a towel"_s);
    QCOMPARE(source->eventsBetween(at(18), at(19), QTimeZone::UTC).first().description,
             u"Bring an apron and a towel"_s);
}

QTEST_GUILESS_MAIN(TestQueuedSource)
#include "tst_queuedsource.moc"
