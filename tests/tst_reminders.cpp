#include "NotificationServer.h"
#include "Reminders.h"

#include "callie/ReminderScheduler.h"
#include "callie/Settings.h"
#include "callie/Times.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

/// Keeps what would be shown, and hands out ids at once.
class FakeServer : public NotificationServer
{
public:
    struct Shown
    {
        uint id;
        QString title;
        QString body;
        QStringList actions;
    };
    QList<Shown> shown;
    QList<uint> closedIds;

    void show(const QString &title, const QString &body, const QStringList &actions,
              std::function<void(uint)> done) override
    {
        const uint id = uint(shown.size()) + 1;
        shown.append({id, title, body, actions});
        done(id);
    }
    void close(uint id) override { closedIds.append(id); }
};

class ListSource : public CalendarSource
{
public:
    QList<Event> events;

    QString sourceId() const override { return u"list"_s; }
    QList<CalendarInfo> calendars() const override { return {}; }
    QList<Event> eventsBetween(const QDateTime &from, const QDateTime &to,
                               const QTimeZone &) const override
    {
        QList<Event> result;
        for (const Event &event : events) {
            if (event.start < to && event.end > from)
                result.append(event);
        }
        return result;
    }
    void refresh() override {}
};

const QTimeZone kBerlin("Europe/Berlin");

QDateTime at(int hour, int minute = 0, int day = 7)
{
    return QDateTime(QDate(2026, 10, day), QTime(hour, minute), kBerlin);
}

Event eventAt(const QString &summary, const QDateTime &start)
{
    Event result;
    result.uid = summary.toLower();
    result.summary = summary;
    result.start = start;
    result.end = start.addSecs(3600);
    return result;
}

} // namespace

class TestReminders : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void logoIsCopiedForTheDaemon();
    void init();
    void sayHowSoon();
    void buttonsFollowTheEvent();
    void openShowsTheEventsDay();
    void snoozeComesBack();
    void turningOffStopsReminders();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<ReminderScheduler> m_scheduler;
    FakeServer m_server;
    ListSource m_source;
    QDateTime m_now;
};

void TestReminders::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_settings = std::make_unique<Settings>(m_dir->filePath(u"settings.ini"_s));
    m_settings->setTimeZoneId(u"Europe/Berlin"_s);
    m_settings->setTimeFormat(Settings::TimeFormat::TwentyFourHour);
    m_server.shown.clear();
    m_server.closedIds.clear();
    m_source.events.clear();
    m_now = at(9);
    m_scheduler = std::make_unique<ReminderScheduler>();
    m_scheduler->setNow([this] { return m_now; });
    m_scheduler->setSource(&m_source);
    Reminders::instance()->setup(m_scheduler.get(), &m_server, m_settings.get());
}

void TestReminders::sayHowSoon()
{
    const Times times(kBerlin, true);
    Event standup = eventAt(u"Standup"_s, at(10));
    QCOMPARE(Reminders::describe(standup, at(9, 50), times).body, u"In 10 min, 10:00 to 11:00"_s);
    QCOMPARE(Reminders::describe(standup, at(10), times).body, u"Now, 10:00 to 11:00"_s);
    QCOMPARE(Reminders::describe(standup, at(7), times).body, u"10:00 to 11:00"_s);
    QCOMPARE(Reminders::describe(standup, at(20, 0, 6), times).body,
             u"Wednesday, 10:00 to 11:00"_s);

    standup.location = u"Room 4"_s;
    QCOMPARE(Reminders::describe(standup, at(9, 59), times).body,
             u"In 1 min, 10:00 to 11:00\nRoom 4"_s);
    standup.summary.clear();
    QCOMPARE(Reminders::describe(standup, at(9, 59), times).title, u"(No title)"_s);

    // An all-day event's day is its date in the chosen zone.
    Event holiday = eventAt(u"Holiday"_s, at(0, 0, 8));
    holiday.allDay = true;
    holiday.end = at(0, 0, 9);
    QCOMPARE(Reminders::describe(holiday, at(1, 0, 8), times).body, u"Today"_s);
    QCOMPARE(Reminders::describe(holiday, at(23, 30, 7), times).body, u"Thursday, October 8"_s);
}

void TestReminders::buttonsFollowTheEvent()
{
    Event call = eventAt(u"Call"_s, at(9, 15));
    call.conferenceUrl = QUrl(u"https://meet.google.com/abc"_s);
    Event lunch = eventAt(u"Lunch"_s, at(9, 16));
    lunch.location = u"<a href=\"https://example.com\">Cafe</a>"_s;
    lunch.conferenceUrl = QUrl(u"javascript:alert(1)"_s);
    // Only the notes hold this call's link.
    Event pairing = eventAt(u"Pairing"_s, at(9, 16));
    pairing.description = u"Join at https://acme.zoom.us/j/123"_s;
    m_source.events = {call, lunch, pairing};
    m_now = at(9, 6);
    m_scheduler->check();

    QCOMPARE(m_server.shown.size(), 3);
    QVERIFY(m_server.shown.at(0).actions.contains(u"join"_s));
    QVERIFY(!m_server.shown.at(1).actions.contains(u"join"_s));
    QVERIFY(m_server.shown.at(2).actions.contains(u"join"_s));
    QVERIFY(m_server.shown.at(1).body.endsWith(
        u"&lt;a href=&quot;https://example.com&quot;&gt;Cafe&lt;/a&gt;"_s));
    for (const FakeServer::Shown &shown : std::as_const(m_server.shown)) {
        QVERIFY(shown.actions.contains(u"snooze"_s));
        QVERIFY(shown.actions.contains(u"default"_s));
    }
}

void TestReminders::openShowsTheEventsDay()
{
    // The day is the event's date in the chosen zone, not in UTC.
    const Event late = eventAt(u"Late"_s, at(0, 30, 8));
    m_source.events = {late};
    m_now = at(0, 20, 8);
    m_scheduler->check();
    QCOMPARE(m_server.shown.size(), 1);

    QSignalSpy opened(Reminders::instance(), &Reminders::openRequested);
    Q_EMIT m_server.actionInvoked(m_server.shown.first().id, u"default"_s);
    QCOMPARE(opened.size(), 1);
    QCOMPARE(opened.first().at(0).toDateTime(), QDate(2026, 10, 8).startOfDay());
    QCOMPARE(opened.first().at(1).toString(), u"late"_s);
    QCOMPARE(opened.first().at(2).toDateTime(), late.start);
    QCOMPARE(m_server.closedIds, QList<uint>{m_server.shown.first().id});

    // A second click on the same, now closed, notification does nothing.
    Q_EMIT m_server.actionInvoked(m_server.shown.first().id, u"default"_s);
    QCOMPARE(opened.size(), 1);
}

void TestReminders::snoozeComesBack()
{
    m_source.events = {eventAt(u"Standup"_s, at(9, 15))};
    m_now = at(9, 5);
    m_scheduler->check();
    QCOMPARE(m_server.shown.size(), 1);

    Q_EMIT m_server.actionInvoked(m_server.shown.first().id, u"snooze"_s);
    m_now = at(9, 9);
    m_scheduler->check();
    QCOMPARE(m_server.shown.size(), 1);
    m_now = at(9, 10);
    m_scheduler->check();
    QCOMPARE(m_server.shown.size(), 2);
    QCOMPARE(m_server.shown.last().body, u"In 5 min, 9:15 to 10:15"_s);
}

void TestReminders::turningOffStopsReminders()
{
    m_settings->setNotify(false);
    m_source.events = {eventAt(u"Standup"_s, at(9, 15))};
    m_now = at(9, 5);
    m_scheduler->check();
    QVERIFY(m_server.shown.isEmpty());

    m_settings->setReminderMinutes(-1);
    m_settings->setNotify(true);
    m_source.events = {eventAt(u"Later"_s, at(9, 30))};
    m_now = at(9, 30);
    m_scheduler->check();
    QVERIFY(m_server.shown.isEmpty());
}

void TestReminders::logoIsCopiedForTheDaemon()
{
    QTemporaryDir dir;
    const QString folder = dir.filePath(u"callie"_s);
    const QString path = folder + u"/notification-icon.png"_s;

    // Missing: written, the folder too.
    QCOMPARE(cachedLogo(folder, "old logo"), path);
    QFile written(path);
    QVERIFY(written.open(QIODevice::ReadOnly));
    QCOMPARE(written.readAll(), QByteArray("old logo"));
    written.close();

    // The same: left alone, which a read-only copy shows, since writing it would fail.
    QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner));
    QCOMPARE(cachedLogo(folder, "old logo"), path);

    // Different: replaced.
    QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    QCOMPARE(cachedLogo(folder, "new logo"), path);
    QVERIFY(written.open(QIODevice::ReadOnly));
    QCOMPARE(written.readAll(), QByteArray("new logo"));
    written.close();

    // Unwritable: nothing to offer.
    QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner));
    QVERIFY(cachedLogo(folder, "newer logo").isEmpty());
}

QTEST_GUILESS_MAIN(TestReminders)
#include "tst_reminders.moc"
