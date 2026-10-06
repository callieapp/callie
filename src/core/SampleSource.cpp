#include "callie/SampleSource.h"

#include <QUuid>

#include <algorithm>

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

SampleSource::SampleSource(QObject *parent) : CalendarSource(parent)
{
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
            e.uid = QStringLiteral("sample-%1-%2-%3")
                        .arg(date.toString(Qt::ISODate), QString::number(s.startHour),
                             QString::number(s.startMinute));
            e.calendarId = m_calendars.at(s.calendar).id;
            e.color = m_calendars.at(s.calendar).color;
            e.summary = QString::fromUtf8(s.summary);
            e.location = QString::fromUtf8(s.location);
            if (s.conference[0] != '\0')
                e.conferenceUrl = QUrl(QString::fromUtf8(s.conference));
            e.start = QDateTime(date, QTime(s.startHour, s.startMinute), tz);
            e.end = e.start.addSecs(s.durationMinutes * 60);
            e.eventId = e.uid;
            e.canEdit = true;
            // Calls are invitations, so their answers can be tried out.
            if (!e.conferenceUrl.isEmpty()) {
                e.attendees = {QStringLiteral("priya@example.com"),
                               QStringLiteral("sam@example.com")};
                e.responseStatus = QStringLiteral("needsAction");
                e.canRespond = true;
            }

            if (e.end > from && e.start < to)
                out.append(e);
        }
    }
    for (Event e : m_created) {
        if (e.end > from && e.start < to) {
            e.start = e.start.toTimeZone(tz);
            e.end = e.end.toTimeZone(tz);
            out.append(e);
        }
    }
    return out;
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
