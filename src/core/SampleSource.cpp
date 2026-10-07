#include "callie/SampleSource.h"

#include <QUuid>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

struct Seed
{
    int dayOfWeek; // Qt::Monday .. Qt::Sunday
    int startHour;
    int startMinute;
    int durationMinutes;
    const char *summary;
    const char *location;
    const char *conference;
    int calendar; // index into m_calendars
};

// A week that exercises the layout: back-to-back meetings, a three-way
// overlap on Wednesday, an early call, and a long focus block.
// clang-format off: the columns are aligned on purpose, so the table stays readable.
constexpr Seed kSeeds[] = {
    { Qt::Monday,    9, 30,  30, "Standup",                  "",            "https://zoom.us/j/1112223334", 0 },
    { Qt::Monday,   11,  0,  60, "1:1 with Priya",           "",            "https://zoom.us/j/9998887776", 0 },
    { Qt::Monday,   14,  0, 120, "Focus: sync engine",       "",            "",                             1 },
    { Qt::Tuesday,   8,  0,  45, "Berlin sync",              "",            "https://meet.google.com/abc-defg-hij", 0 },
    { Qt::Tuesday,   9, 30,  30, "Standup",                  "",            "https://zoom.us/j/1112223334", 0 },
    { Qt::Tuesday,  13,  0,  60, "Design review",            "Studio",     "",                             0 },
    { Qt::Wednesday, 9, 30,  30, "Standup",                  "",            "https://zoom.us/j/1112223334", 0 },
    { Qt::Wednesday,10,  0,  90, "Roadmap workshop",         "Room 2",     "",                             0 },
    { Qt::Wednesday,10, 30,  60, "Vendor call",              "",            "https://zoom.us/j/5554443332", 0 },
    { Qt::Wednesday,11,  0,  30, "Quick sync w/ Sam",        "",            "",                             1 },
    { Qt::Wednesday,16,  0,  60, "Dentist",                  "Clinic",     "",                             2 },
    { Qt::Thursday,  9, 30,  30, "Standup",                  "",            "https://zoom.us/j/1112223334", 0 },
    { Qt::Thursday, 12,  0,  60, "Lunch with Alex",          "Cafe Sol",   "",                             2 },
    { Qt::Thursday, 15,  0, 180, "Focus: QML views",         "",            "",                             1 },
    { Qt::Friday,    9, 30,  30, "Standup",                  "",            "https://zoom.us/j/1112223334", 0 },
    { Qt::Friday,   11,  0,  60, "Retro",                    "",            "https://meet.google.com/xyz-uvwx-yz", 0 },
    { Qt::Friday,   17,  0,  90, "Climbing",                 "The Wall",   "",                             2 },
    { Qt::Sunday,   10,  0, 120, "Farmers market",           "",            "",                             2 },
};
// clang-format on

} // namespace

SampleSource::SampleSource(QObject *parent)
    : CalendarSource(parent), m_now([] { return QDateTime::currentDateTimeUtc(); })
{
    m_synced = m_now();
    m_calendars = {
        {QStringLiteral("work"), QStringLiteral("Work"), QColor(QStringLiteral("#5B8DEF")), true,
         true, QStringLiteral("sam@work.example")},
        {QStringLiteral("focus"), QStringLiteral("Focus"), QColor(QStringLiteral("#7C6BD6")), true,
         true, QStringLiteral("sam@work.example")},
        {QStringLiteral("personal"), QStringLiteral("Personal"), QColor(QStringLiteral("#2FA98C")),
         true, true, QStringLiteral("sam@home.example")},
    };
}

QList<Event> SampleSource::eventsBetween(const QDateTime &from, const QDateTime &to,
                                         const QTimeZone &tz) const
{
    QList<Event> out;
    // Anchor the seed week to the Monday on or before `from`.
    QDate monday = from.date().addDays(-(from.date().dayOfWeek() - Qt::Monday));

    for (QDate week = monday.addDays(-7); week < to.date().addDays(7); week = week.addDays(7)) {
        for (const Seed &s : kSeeds) {
            const QDate date = week.addDays(s.dayOfWeek - Qt::Monday);
            Event e;
            // Each seed repeats weekly, so its occurrences share a uid, as a
            // series' do; the event id names the one occurrence.
            e.uid = QStringLiteral("sample-%1-%2-%3")
                        .arg(QString::number(s.dayOfWeek), QString::number(s.startHour),
                             QString::number(s.startMinute));
            e.calendarId = m_calendars.at(s.calendar).id;
            e.color = m_calendars.at(s.calendar).color;
            e.summary = QString::fromUtf8(s.summary);
            e.location = QString::fromUtf8(s.location);
            if (s.conference[0] != '\0')
                e.conferenceUrl = QUrl(QString::fromUtf8(s.conference));
            e.start = QDateTime(date, QTime(s.startHour, s.startMinute), tz);
            e.end = e.start.addSecs(s.durationMinutes * 60);
            e.eventId = e.uid + u'-' + date.toString(Qt::ISODate);
            // Every seed repeats weekly on its day.
            static const char *const kDays[] = {"MO", "TU", "WE", "TH", "FR", "SA", "SU"};
            e.recurrence = {u"RRULE:FREQ=WEEKLY;BYDAY="_s +
                            QLatin1StringView(kDays[s.dayOfWeek - 1])};
            e.canEdit = true;
            // Calls are invitations, so their answers can be tried out.
            if (!e.conferenceUrl.isEmpty()) {
                e.attendees = {QStringLiteral("priya@example.com"),
                               QStringLiteral("jordan@example.com")};
                // Most are accepted; one is a maybe and two wait for an answer,
                // to show how each looks.
                const QByteArray summary(s.summary);
                e.responseStatus = summary == "Retro"         ? QStringLiteral("tentative")
                                   : summary == "Vendor call" ? QStringLiteral("needsAction")
                                   : summary == "Berlin sync" ? QStringLiteral("needsAction")
                                                              : QStringLiteral("accepted");
                e.guests = {
                    {QStringLiteral("priya@example.com"), QStringLiteral("Priya"),
                     QStringLiteral("accepted"), true, false},
                    {QStringLiteral("jordan@example.com"), QStringLiteral("Jordan"),
                     QStringLiteral("needsAction"), false, false},
                    {QStringLiteral("sam@work.example"), QStringLiteral("Sam"), e.responseStatus,
                     false, true},
                };
                e.canRespond = true;
                // Calls repeat weekly, so their occurrences share a series.
                e.seriesId = QStringLiteral("sample-") + QString::fromUtf8(s.summary);
            }

            for (const Edit &change : m_edits) {
                const bool inSeries = !change.edited.seriesId.isEmpty()
                                          ? e.seriesId == change.edited.seriesId
                                          : e.uid == change.edited.uid;
                const bool hit = change.scope == EditScope::AllEvents ? inSeries
                                 : change.scope == EditScope::ThisAndFollowing
                                     ? inSeries && e.start >= change.edited.start
                                     : e.eventId == change.edited.eventId;
                if (hit)
                    applyEdit(e, change.edited, change.edit);
            }
            if (const auto moved = m_moved.constFind(e.eventId); moved != m_moved.cend()) {
                e.start = moved->first.toTimeZone(tz);
                e.end = moved->second.toTimeZone(tz);
            }
            if (m_answers.contains(e.eventId)) {
                e.responseStatus = m_answers.value(e.eventId);
                for (Guest &guest : e.guests) {
                    if (guest.self)
                        guest.response = e.responseStatus;
                }
                e.declined = e.responseStatus == u"declined";
            }
            if (e.end > from && e.start < to && !m_deleted.contains(e.eventId))
                out.append(e);
        }
    }
    for (Event e : m_created) {
        if (e.end > from && e.start < to && !m_deleted.contains(e.eventId)) {
            e.start = e.start.toTimeZone(tz);
            e.end = e.end.toTimeZone(tz);
            out.append(e);
        }
    }
    return out;
}

void SampleSource::respond(const Event &event, const QString &status, bool, Created done)
{
    m_answers.insert(event.eventId, status);
    done({});
    Q_EMIT changed();
}

void SampleSource::moveEvent(const Event &event, const QDateTime &start, const QDateTime &end, bool,
                             Created done)
{
    m_moved.insert(event.eventId, {start, end});
    for (Event &created : m_created) {
        if (created.eventId == event.eventId) {
            created.start = start;
            created.end = end;
        }
    }
    done({});
    Q_EMIT changed();
}

void SampleSource::updateEvent(const Event &event, const EventEdit &edit, EditScope scope,
                               Created done)
{
    for (Event &created : m_created) {
        if (created.eventId == event.eventId) {
            applyEdit(created, event, edit);
            done({});
            Q_EMIT changed();
            return;
        }
    }
    m_edits.append({event, edit, scope});
    done({});
    Q_EMIT changed();
}

void SampleSource::deleteEvent(const Event &event, bool, Created done)
{
    m_deleted.insert(event.eventId);
    done({});
    Q_EMIT changed();
}

void SampleSource::setNow(std::function<QDateTime()> now)
{
    m_now = std::move(now);
    m_synced = m_now();
    Q_EMIT statusChanged();
}

void SampleSource::refresh()
{
    m_synced = m_now();
    Q_EMIT statusChanged();
    Q_EMIT changed();
}

QVariantList SampleSource::syncReport() const
{
    QVariantList report;
    QStringList accounts;
    for (const CalendarInfo &calendar : m_calendars) {
        if (!accounts.contains(calendar.account))
            accounts << calendar.account;
    }
    for (const QString &account : accounts) {
        report << QVariantMap{{QStringLiteral("account"), account},
                              {QStringLiteral("lastSynced"), m_synced},
                              {QStringLiteral("error"), QString()},
                              {QStringLiteral("problems"), QStringList()}};
    }
    return report;
}

void SampleSource::createEvent(const EventDraft &draft, Created done)
{
    const auto calendar =
        std::find_if(m_calendars.cbegin(), m_calendars.cend(),
                     [&draft](const CalendarInfo &c) { return c.id == draft.calendarId; });
    if (calendar == m_calendars.cend()) {
        done(tr("No such calendar."));
        return;
    }
    Event e;
    e.uid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    e.calendarId = calendar->id;
    e.color = calendar->color;
    e.summary = draft.summary;
    e.location = draft.location;
    e.description = draft.description;
    e.start = draft.start;
    e.end = draft.end;
    e.allDay = draft.allDay;
    e.eventId = e.uid;
    e.canEdit = true;
    m_created.append(e);
    done({});
    Q_EMIT changed();
}

} // namespace callie
