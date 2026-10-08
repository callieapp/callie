#include "Commands.h"

#include "callie/QuickAdd.h"
#include "callie/Repeat.h"
#include "callie/SearchModel.h"
#include "callie/Settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace callie::cli {

namespace {

constexpr int kDaysAround = 366;
constexpr int kInviteDays = 90;

bool byStart(const Event &a, const Event &b)
{
    return a.start < b.start;
}

} // namespace

Commands::Commands(CalendarSource &source, Settings &settings, QTextStream &out, QTextStream &err,
                   QDateTime now, QTimeZone zone)
    : m_source(source), m_settings(settings), m_out(out), m_err(err), m_now(std::move(now)),
      m_zone(std::move(zone))
{}

int Commands::agenda(int days, bool json)
{
    const QDateTime from(m_now.toTimeZone(m_zone).date(), QTime(0, 0), m_zone);
    QList<Event> events = m_source.eventsBetween(from, from.addDays(std::max(1, days)), m_zone);
    std::sort(events.begin(), events.end(), byStart);
    if (events.isEmpty() && !json) {
        m_err << tr("Nothing scheduled.") << "\n";
        return 0;
    }
    print(events, json, true);
    return 0;
}

int Commands::search(const QString &query, bool json)
{
    const QStringList words = query.split(u' ', Qt::SkipEmptyParts);
    if (words.isEmpty()) {
        m_err << tr("usage: callie search <words>") << "\n";
        return 2;
    }
    const QList<Event> all =
        m_source.eventsBetween(m_now.addDays(-kDaysAround), m_now.addDays(kDaysAround), m_zone);
    // A repeating event once: its next occurrence, or its last if all have passed.
    QHash<QString, Event> found;
    for (const Event &e : all) {
        if (!SearchModel::matches(e, words))
            continue;
        const QString key = e.calendarId + u'/' + (e.seriesId.isEmpty() ? e.uid : e.seriesId);
        const auto seen = found.constFind(key);
        const bool upcoming = e.end > m_now;
        if (seen == found.cend() || (upcoming ? (seen->end <= m_now || e.start < seen->start)
                                              : (seen->end <= m_now && e.start > seen->start)))
            found.insert(key, e);
    }
    QList<Event> results = found.values();
    std::sort(results.begin(), results.end(), [this](const Event &a, const Event &b) {
        const bool aUp = a.end > m_now;
        const bool bUp = b.end > m_now;
        if (aUp != bUp)
            return aUp;
        return aUp ? a.start < b.start : a.start > b.start;
    });
    if (results.isEmpty()) {
        if (!json) {
            m_err << tr("No events match.") << "\n";
            return 0;
        }
    }
    print(results, json, false);
    return 0;
}

int Commands::invites(bool json)
{
    QList<Event> pending;
    QSet<QString> series;
    for (const Event &e : m_source.eventsBetween(m_now, m_now.addDays(kInviteDays), m_zone)) {
        if (e.responseStatus != u"needsAction" || !e.canRespond)
            continue;
        // A repeating invitation is answered once, so it is listed once.
        const QString key = e.calendarId + u'/' + (e.seriesId.isEmpty() ? e.eventId : e.seriesId);
        if (series.contains(key))
            continue;
        series.insert(key);
        pending.append(e);
    }
    std::sort(pending.begin(), pending.end(), byStart);
    if (pending.isEmpty() && !json) {
        m_err << tr("No invitations waiting.") << "\n";
        return 0;
    }
    print(pending, json, false);
    return 0;
}

void Commands::print(const QList<Event> &events, bool json, bool byDay)
{
    const auto paint = [this](const char *code, const QString &s) {
        return m_color ? u"\033["_s + QLatin1StringView(code) + u'm' + s + u"\033[0m"_s : s;
    };
    const QDate today = m_now.toTimeZone(m_zone).date();
    QDate current;
    for (const Event &e : events) {
        const QDateTime start = e.start.toTimeZone(m_zone);
        const QDateTime end = e.end.toTimeZone(m_zone);
        if (json)
            continue;
        // Events already under way when the range starts are listed under today.
        const QDate day = byDay ? std::max(start.date(), today) : start.date();
        if (byDay && day != current) {
            current = day;
            const QString label =
                day == today ? tr("Today") : QLocale().toString(day, QStringLiteral("ddd d MMM"));
            m_out << "\n" << paint("1", label) << "\n";
        }
        QString time = e.allDay ? u"all-day"_s
                                : start.toString(u"HH:mm"_s) + u"–"_s + end.toString(u"HH:mm"_s);
        time = time.leftJustified(11);
        if (!byDay)
            time = QLocale().toString(day, u"ddd d MMM"_s).leftJustified(10) + u"  "_s + time;
        m_out << u"  "_s << paint("2", time) << u"  "_s << e.summary;
        if (!e.conferenceUrl.isEmpty())
            m_out << "  " << paint("36", u"↗"_s);
        else if (!e.location.isEmpty())
            m_out << "  " << paint("2", e.location);
        m_out << "\n";
    }
    if (byDay && !json)
        m_out << "\n";
    if (!json)
        return;
    QJsonArray list;
    for (const Event &e : events) {
        const auto time = [&e, this](const QDateTime &t) {
            return e.allDay ? t.toTimeZone(m_zone).date().toString(Qt::ISODate)
                            : t.toTimeZone(m_zone).toString(Qt::ISODate);
        };
        list.append(QJsonObject{{u"id"_s, reference(e)},
                                {u"calendar"_s, e.calendarId},
                                {u"title"_s, e.summary},
                                {u"start"_s, time(e.start)},
                                {u"end"_s, time(e.end)},
                                {u"allDay"_s, e.allDay},
                                {u"location"_s, e.location},
                                {u"response"_s, e.responseStatus},
                                {u"repeats"_s, !e.seriesId.isEmpty()}});
    }
    m_out << QJsonDocument(list).toJson(QJsonDocument::Indented);
}

QString Commands::reference(const Event &event)
{
    return event.calendarId + u'/' + event.eventId;
}

std::optional<QDateTime> Commands::when(const QString &text, QDate day) const
{
    const QString trimmed = text.trimmed();
    for (const QString &format :
         {u"yyyy-MM-dd HH:mm"_s, u"yyyy-MM-dd'T'HH:mm"_s, u"yyyy-MM-dd HH:mm:ss"_s}) {
        const QDateTime at = QDateTime::fromString(trimmed, format);
        if (at.isValid())
            return QDateTime(at.date(), at.time(), m_zone);
    }
    if (const QDate date = QDate::fromString(trimmed, Qt::ISODate); date.isValid())
        return QDateTime(date, QTime(0, 0), m_zone);
    if (const QTime time = QTime::fromString(trimmed, u"H:mm"_s); time.isValid() && day.isValid())
        return QDateTime(day, time, m_zone);
    // Words, as `callie add` reads them: "friday 3pm", "tomorrow".
    const EventDraft parsed = QuickAdd::parse(u"x "_s + trimmed, m_now, m_zone);
    if (!parsed.timeGuessed && parsed.start.isValid() && parsed.summary == u"x")
        return parsed.start.toTimeZone(m_zone);
    return std::nullopt;
}

std::optional<Event> Commands::find(const QString &id)
{
    // "calendar/event", as --json gives it, or the event alone when only one
    // calendar has it.
    const qsizetype slash = id.lastIndexOf(u'/');
    const QString calendar = slash < 0 ? QString() : id.left(slash);
    const QString eventId = id.mid(slash + 1);
    const QList<Event> all =
        m_source.eventsBetween(m_now.addDays(-kDaysAround), m_now.addDays(kDaysAround), m_zone);
    QList<Event> found;
    for (const Event &e : all) {
        if (e.eventId == eventId && (calendar.isEmpty() || e.calendarId == calendar))
            found.append(e);
    }
    if (found.size() == 1)
        return found.first();
    if (found.isEmpty())
        m_err << tr("callie: no event with the id %1; callie agenda --json lists them").arg(id)
              << "\n";
    else
        m_err << tr("callie: %1 is in more than one calendar; give it as %2")
                     .arg(id, reference(found.first()))
              << "\n";
    return std::nullopt;
}

std::optional<EditScope> Commands::scopeOf(const QString &scope)
{
    if (scope.isEmpty() || scope == u"this")
        return EditScope::ThisEvent;
    if (scope == u"following")
        return EditScope::ThisAndFollowing;
    if (scope == u"all")
        return EditScope::AllEvents;
    m_err << tr("callie: --scope is this, following or all") << "\n";
    return std::nullopt;
}

QString Commands::writableCalendar(const QString &wanted) const
{
    const QList<CalendarInfo> calendars = m_source.calendars();
    const auto writable = [&calendars](const QString &name) -> QString {
        for (const CalendarInfo &c : calendars) {
            if (c.writable &&
                (c.id == name || c.displayName.compare(name, Qt::CaseInsensitive) == 0))
                return c.id;
        }
        return {};
    };
    if (!wanted.isEmpty())
        return writable(wanted);
    for (const QString &choice : {m_settings.defaultCalendar(), m_settings.newEventCalendar()}) {
        if (const QString id = writable(choice); !choice.isEmpty() && !id.isEmpty())
            return id;
    }
    const auto first = std::find_if(calendars.cbegin(), calendars.cend(),
                                    [](const CalendarInfo &c) { return c.writable; });
    return first == calendars.cend() ? QString() : first->id;
}

void Commands::report(const Outcome &outcome, const Done &done)
{
    if (outcome.error.isEmpty()) {
        done(0);
        return;
    }
    m_err << "callie: " << outcome.error << "\n";
    done(1);
}

void Commands::add(const QString &text, const QString &calendarId, const Done &done)
{
    EventDraft draft = QuickAdd::parse(text, m_now, m_zone);
    if (!draft.isValid()) {
        m_err << tr("callie: could not read an event from that; try \"Lunch tomorrow 12-1pm\"")
              << "\n";
        done(2);
        return;
    }
    draft.calendarId = writableCalendar(calendarId);
    if (draft.calendarId.isEmpty()) {
        m_err << (calendarId.isEmpty()
                      ? tr("callie: no calendar can take new events")
                      : tr("callie: no calendar you can add to is called %1").arg(calendarId))
              << "\n";
        done(1);
        return;
    }
    m_settings.setNewEventCalendar(draft.calendarId);
    m_source.createEvent(draft, [this, done](const Outcome &outcome) { report(outcome, done); });
}

void Commands::edit(const QString &id, const EditOptions &options, const QString &scopeText,
                    const Done &done)
{
    const std::optional<Event> found = find(id);
    if (!found) {
        done(1);
        return;
    }
    const Event &event = *found;
    std::optional<EditScope> scope = scopeOf(scopeText);
    if (!scope) {
        done(2);
        return;
    }
    if (event.seriesId.isEmpty())
        scope = EditScope::ThisEvent;
    const auto mistake = [this, &done](const QString &message) {
        m_err << "callie: " << message << "\n";
        done(2);
    };

    EventEdit edit;
    edit.summary = options.title;
    edit.location = options.where;
    edit.description = options.notes;
    edit.guests = options.guests;
    edit.videoCall = options.video;

    if (options.start || options.end || options.allDay) {
        const bool allDay = options.allDay.value_or(event.allDay);
        const QDateTime wasStart = event.start.toTimeZone(m_zone);
        const QDateTime wasEnd = event.end.toTimeZone(m_zone);
        std::optional<QDateTime> start =
            options.start ? when(*options.start, wasStart.date()) : std::optional(wasStart);
        if (!start)
            return mistake(tr("could not read the start %1").arg(*options.start));
        std::optional<QDateTime> end;
        if (options.end) {
            end = when(*options.end, start->date());
            if (!end)
                return mistake(tr("could not read the end %1").arg(*options.end));
            // An all-day event's end names its last day.
            if (allDay)
                end = end->addDays(1);
        } else if (options.start) {
            // A new start keeps the length, in days for an all-day event.
            end = allDay != event.allDay ? start->addSecs(3600)
                  : allDay               ? start->addDays(wasStart.date().daysTo(wasEnd.date()))
                                         : start->addSecs(wasStart.secsTo(wasEnd));
        } else {
            end = wasEnd;
        }
        if (allDay) {
            const QDate first = start->date();
            const QDate after = end->time() == QTime(0, 0) ? end->date() : end->date().addDays(1);
            start = QDateTime(first, QTime(0, 0), m_zone);
            end = QDateTime(std::max(after, first.addDays(1)), QTime(0, 0), m_zone);
        } else if (event.allDay && !options.start) {
            // Timed now, with no start given: from nine, for an hour unless an
            // end was given.
            start = QDateTime(wasStart.date(), QTime(9, 0), m_zone);
            if (!options.end)
                end = start->addSecs(3600);
        }
        if (*end <= *start)
            return mistake(tr("the end must come after the start"));
        edit.start = *start;
        edit.end = *end;
        edit.allDay = allDay;
    }

    if (options.repeat) {
        if (!Repeat::choices().contains(*options.repeat))
            return mistake(tr("--repeat is one of %1").arg(Repeat::choices().join(u", "_s)));
        if (!event.seriesId.isEmpty() && *scope == EditScope::ThisEvent)
            return mistake(tr("a repeat is for the whole series; add --scope all or following"));
        const QDate day = (edit.start ? *edit.start : event.start).toTimeZone(m_zone).date();
        edit.recurrence = Repeat::rule(*options.repeat, day);
    }

    if (edit.isEmpty())
        return mistake(tr("nothing to change; callie edit --help lists what can be"));
    m_source.updateEvent(event, edit, *scope,
                         [this, done](const Outcome &outcome) { report(outcome, done); });
}

void Commands::remove(const QString &id, const QString &scopeText, const Done &done)
{
    const std::optional<Event> event = find(id);
    if (!event) {
        done(1);
        return;
    }
    const std::optional<EditScope> scope = scopeOf(scopeText);
    if (!scope || *scope == EditScope::ThisAndFollowing) {
        if (scope)
            m_err << tr("callie: delete takes --scope this or all") << "\n";
        done(2);
        return;
    }
    m_source.deleteEvent(*event, *scope == EditScope::AllEvents,
                         [this, done](const Outcome &outcome) { report(outcome, done); });
}

void Commands::respond(const QString &id, const QString &answer, const QString &scopeText,
                       const Done &done)
{
    static const QHash<QString, QString> kAnswers = {
        {u"yes"_s, u"accepted"_s},        {u"maybe"_s, u"tentative"_s},
        {u"no"_s, u"declined"_s},         {u"accepted"_s, u"accepted"_s},
        {u"tentative"_s, u"tentative"_s}, {u"declined"_s, u"declined"_s}};
    const QString status = kAnswers.value(answer.toLower());
    if (status.isEmpty()) {
        m_err << tr("callie: answer yes, maybe or no") << "\n";
        done(2);
        return;
    }
    const std::optional<Event> event = find(id);
    if (!event) {
        done(1);
        return;
    }
    if (!event->canRespond) {
        m_err << tr("callie: %1 has no invitation to answer").arg(event->summary) << "\n";
        done(1);
        return;
    }
    const std::optional<EditScope> scope = scopeOf(scopeText);
    if (!scope || *scope == EditScope::ThisAndFollowing) {
        if (scope)
            m_err << tr("callie: an answer takes --scope this or all") << "\n";
        done(2);
        return;
    }
    m_source.respond(*event, status, *scope == EditScope::AllEvents,
                     [this, done](const Outcome &outcome) { report(outcome, done); });
}

void Commands::duplicate(const QString &id, const std::optional<QString> &start, const Done &done)
{
    const std::optional<Event> event = find(id);
    if (!event) {
        done(1);
        return;
    }
    EventDraft draft;
    draft.summary = event->summary;
    draft.location = event->location;
    draft.description = event->description;
    draft.allDay = event->allDay;
    draft.start = event->start.toTimeZone(m_zone);
    draft.end = event->end.toTimeZone(m_zone);
    if (start) {
        const std::optional<QDateTime> at = when(*start, draft.start.date());
        if (!at) {
            m_err << tr("callie: could not read the start %1").arg(*start) << "\n";
            done(2);
            return;
        }
        if (draft.allDay) {
            const qint64 days = draft.start.date().daysTo(draft.end.date());
            draft.start = QDateTime(at->date(), QTime(0, 0), m_zone);
            draft.end = QDateTime(at->date().addDays(days), QTime(0, 0), m_zone);
        } else {
            draft.end = at->addSecs(draft.start.secsTo(draft.end));
            draft.start = *at;
        }
    }
    const QList<CalendarInfo> calendars = m_source.calendars();
    const bool own =
        std::any_of(calendars.cbegin(), calendars.cend(), [&event](const CalendarInfo &c) {
            return c.id == event->calendarId && c.writable;
        });
    draft.calendarId = own ? event->calendarId : writableCalendar({});
    if (draft.calendarId.isEmpty()) {
        m_err << tr("callie: no calendar can take a copy") << "\n";
        done(1);
        return;
    }
    m_source.createEvent(draft, [this, done](const Outcome &outcome) { report(outcome, done); });
}

} // namespace callie::cli
