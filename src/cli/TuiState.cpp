#include "TuiState.h"

#include "Commands.h"

#include "callie/QueuedSource.h"
#include "callie/SearchModel.h"
#include "callie/Settings.h"
#include "callie/Times.h"

#include <QTextStream>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace callie::cli {

namespace {

/// How far the agenda reads ahead.
constexpr int kAgendaDays = 14;

} // namespace

TuiState::TuiState(CalendarSource &source, Settings &settings, Now now, QObject *parent)
    : QObject(parent), m_source(&source), m_settings(settings), m_now(std::move(now))
{
    m_day = today();
    connect(m_source, &CalendarSource::changed, this, &TuiState::reload);
    // A sync that finds nothing new, or fails, says so only here.
    connect(m_source, &CalendarSource::statusChanged, this, &TuiState::changed);
    connect(&m_settings, &Settings::hiddenCalendarsChanged, this, &TuiState::reload);
    // Each change can be taken back while it is held.
    connect(m_source, &CalendarSource::changeMade, this,
            [this](const QString &id, const QString &what) {
                m_lastChange = id;
                say(tr("%1. u takes it back.").arg(what));
            });
    reload();
    selectFirstOf(m_day);
}

QDate TuiState::today() const
{
    return m_settings.times()->date(m_now()).date();
}

QDate TuiState::rangeStart() const
{
    switch (m_view) {
    case View::Day:
    case View::Agenda: return m_day;
    case View::Week: {
        const int back = (m_day.dayOfWeek() - m_settings.firstDayOfWeek() + 7) % 7;
        return m_day.addDays(-back);
    }
    }
    return m_day;
}

int TuiState::rangeDays() const
{
    return m_view == View::Day ? 1 : m_view == View::Week ? 7 : kAgendaDays;
}

QDate TuiState::dayOf(const Event &event) const
{
    const QDate first = m_settings.times()->date(event.start).date();
    return std::max(first, rangeStart());
}

void TuiState::reload()
{
    const std::optional<Event> was =
        m_selected >= 0 ? std::optional(m_events.at(m_selected)) : std::nullopt;
    const QDate start = rangeStart();
    const QDateTime from = m_settings.times()->at(start.startOfDay(), 0);
    const QDateTime to = m_settings.times()->at(start.addDays(rangeDays()).startOfDay(), 0);
    const QStringList hidden = m_settings.hiddenCalendars();
    QList<Event> events =
        m_source ? m_source->eventsBetween(from, to, m_settings.times()->zone()) : QList<Event>();
    events.removeIf([&](const Event &e) {
        return e.workPlace || hidden.contains(e.calendarId) ||
               (e.declined && !m_settings.showDeclined());
    });
    std::sort(events.begin(), events.end(), [this](const Event &a, const Event &b) {
        const QDate dayA = dayOf(a), dayB = dayOf(b);
        if (dayA != dayB)
            return dayA < dayB;
        if (a.allDay != b.allDay)
            return a.allDay;
        return a.start != b.start ? a.start < b.start : a.summary < b.summary;
    });
    m_events = events;
    // The same event stays selected when it is still shown.
    m_selected = -1;
    if (was) {
        for (int i = 0; i < m_events.size(); ++i) {
            if (m_events.at(i).eventId == was->eventId)
                m_selected = i;
        }
    }
    if (m_selected < 0 && !m_events.isEmpty())
        m_selected = 0;
    Q_EMIT changed();
}

void TuiState::selectFirstOf(QDate day)
{
    m_selected = m_events.isEmpty() ? -1 : 0;
    // Today, the next event still to come; another day, its first.
    const QDateTime now = m_now();
    for (int i = 0; i < m_events.size(); ++i) {
        const Event &e = m_events.at(i);
        if (dayOf(e) < day)
            continue;
        if (day == today() && dayOf(e) == day && e.end <= now)
            continue;
        m_selected = i;
        break;
    }
    Q_EMIT changed();
}

void TuiState::step(int direction)
{
    m_day = m_day.addDays(direction * (m_view == View::Week ? 7 : m_view == View::Day ? 1 : 7));
    reload();
    selectFirstOf(m_view == View::Day ? m_day : rangeStart());
}

void TuiState::setView(View view)
{
    m_view = view;
    reload();
    selectFirstOf(m_day);
}

bool TuiState::press(const QString &key)
{
    if (m_help) {
        if (key == u"?" || key == u"Escape" || key == u"q") {
            m_help = false;
            Q_EMIT changed();
            return true;
        }
        return false;
    }
    switch (m_mode) {
    case Mode::Typing: return pressTyping(key);
    case Mode::Details: return pressDetails(key);
    case Mode::Found: return pressFound(key);
    case Mode::Calendars: return pressCalendars(key);
    case Mode::Browse: break;
    }
    return pressBrowsing(key);
}

bool TuiState::pressBrowsing(const QString &key)
{
    // The second key of a command.
    const QString pending = std::exchange(m_pending, QString());
    if (pending == u"leader") {
        if (key == u"d")
            setView(View::Day);
        else if (key == u"w")
            setView(View::Week);
        else if (key == u"a")
            setView(View::Agenda);
        else
            Q_EMIT changed();
        return true;
    }
    if (pending == u"g") {
        if (key == u"g" && !m_events.isEmpty()) {
            m_selected = 0;
            Q_EMIT changed();
            return true;
        }
        Q_EMIT changed();
    }

    if (key == m_settings.leaderKey()) {
        m_pending = u"leader"_s;
    } else if (key == u"g") {
        m_pending = u"g"_s;
    } else if (key == u"h" || key == u"Left") {
        step(-1);
        return true;
    } else if (key == u"l" || key == u"Right") {
        step(1);
        return true;
    } else if (key == u"t" || key == u"Home") {
        m_day = today();
        reload();
        selectFirstOf(m_day);
        return true;
    } else if (key == u"j" || key == u"Down") {
        if (m_selected + 1 < m_events.size())
            ++m_selected;
    } else if (key == u"k" || key == u"Up") {
        if (m_selected > 0)
            --m_selected;
    } else if (key == u"G" || key == u"End") {
        m_selected = int(m_events.size()) - 1;
    } else if (key == u"Tab") {
        setView(m_view == View::Day ? View::Week : m_view == View::Week ? View::Agenda : View::Day);
        return true;
    } else if (key == u"Enter") {
        if (m_selected < 0)
            return false;
        m_mode = Mode::Details;
    } else if (key == u"n" || key == u"/") {
        m_mode = Mode::Typing;
        m_typing = key == u"n" ? Typing::NewEvent : Typing::Search;
        m_input.clear();
    } else if (key == u"i") {
        // Invitations still waiting, as callie invites lists them.
        const QDateTime now = m_now();
        QList<Event> waiting;
        for (const Event &e :
             m_source->eventsBetween(now, now.addDays(90), m_settings.times()->zone())) {
            if (e.responseStatus == u"needsAction" && e.canRespond &&
                !m_settings.hiddenCalendars().contains(e.calendarId))
                waiting.append(e);
        }
        showFound(tr("Invitations"), waiting);
        return true;
    } else if (key == u"c") {
        m_mode = Mode::Calendars;
        m_calendarSelected = 0;
    } else if (key == u"u") {
        if (m_lastChange.isEmpty() || !m_source->undoChange(m_lastChange)) {
            say(tr("Nothing to take back."), true);
            return true;
        }
        m_lastChange.clear();
        say(tr("Taken back."));
        return true;
    } else if (key == u"r") {
        m_source->refresh();
    } else if (key == u"?") {
        m_help = true;
    } else if (key == u"q") {
        m_quit = true;
    } else {
        return false;
    }
    Q_EMIT changed();
    return true;
}

bool TuiState::pressDetails(const QString &key)
{
    const std::optional<Event> event = current();
    if (!event || key == u"Escape" || key == u"q" || key == u"Enter") {
        m_mode = Mode::Browse;
        Q_EMIT changed();
        return true;
    }
    static const QHash<QString, QString> answers = {
        {u"y"_s, u"yes"_s}, {u"m"_s, u"maybe"_s}, {u"n"_s, u"no"_s}};
    if (answers.contains(key)) {
        if (!event->canRespond) {
            say(tr("%1 is not an invitation.").arg(event->summary), true);
            return true;
        }
        const QString id = Commands::reference(*event);
        const QString answer = answers.value(key);
        act([id, answer](Commands &commands, const std::function<void(int)> &done) {
            commands.respond(id, answer, {}, done);
        });
        return true;
    }
    if (key == u"x") {
        if (!event->canEdit) {
            say(tr("%1 cannot be changed from here.").arg(event->summary), true);
            return true;
        }
        const QString id = Commands::reference(*event);
        m_mode = Mode::Browse;
        act([id](Commands &commands, const std::function<void(int)> &done) {
            commands.remove(id, {}, done);
        });
        return true;
    }
    if (key == u"o") {
        const QUrl url = event->joinUrl();
        if (url.isEmpty()) {
            say(tr("%1 has no call to join.").arg(event->summary), true);
            return true;
        }
        Q_EMIT openRequested(url);
        return true;
    }
    if (key == u"j" || key == u"Down" || key == u"k" || key == u"Up")
        return pressBrowsing(key);
    return false;
}

bool TuiState::pressTyping(const QString &key)
{
    if (key == u"Escape") {
        m_mode = Mode::Browse;
    } else if (key == u"Enter") {
        submit();
        return true;
    } else if (key == u"Backspace") {
        m_input.chop(1);
    } else if (key.size() == 1 && key.at(0).isPrint()) {
        m_input += key;
    } else {
        return false;
    }
    Q_EMIT changed();
    return true;
}

bool TuiState::pressFound(const QString &key)
{
    if (key == u"Escape" || key == u"q") {
        m_mode = Mode::Browse;
    } else if (key == u"j" || key == u"Down") {
        if (m_foundSelected + 1 < m_found.size())
            ++m_foundSelected;
    } else if (key == u"k" || key == u"Up") {
        if (m_foundSelected > 0)
            --m_foundSelected;
    } else if (key == u"Enter") {
        if (m_foundSelected < 0)
            return false;
        jumpTo(m_found.at(m_foundSelected));
        return true;
    } else {
        return false;
    }
    Q_EMIT changed();
    return true;
}

bool TuiState::pressCalendars(const QString &key)
{
    const QList<CalendarInfo> all = calendars();
    if (key == u"Escape" || key == u"q" || key == u"c") {
        m_mode = Mode::Browse;
    } else if (key == u"j" || key == u"Down") {
        if (m_calendarSelected + 1 < all.size())
            ++m_calendarSelected;
    } else if (key == u"k" || key == u"Up") {
        if (m_calendarSelected > 0)
            --m_calendarSelected;
    } else if (key == u" " || key == u"Enter") {
        if (m_calendarSelected >= all.size())
            return false;
        const CalendarInfo &calendar = all.at(m_calendarSelected);
        m_settings.setCalendarVisible(calendar.id, !calendarShown(calendar));
    } else {
        return false;
    }
    Q_EMIT changed();
    return true;
}

void TuiState::submit()
{
    const QString text = m_input.trimmed();
    m_mode = Mode::Browse;
    if (text.isEmpty()) {
        Q_EMIT changed();
        return;
    }
    if (m_typing == Typing::NewEvent) {
        act([text](Commands &commands, const std::function<void(int)> &done) {
            commands.add(text, {}, done);
        });
        return;
    }
    // A search, a year either side, as callie search reads.
    const QStringList words = text.split(u' ', Qt::SkipEmptyParts);
    const QDateTime now = m_now();
    // A repeating event once, as callie search lists it: its next occurrence,
    // or its last if all have passed.
    QHash<QString, Event> found;
    for (const Event &e :
         m_source->eventsBetween(now.addDays(-365), now.addDays(365), m_settings.times()->zone())) {
        if (!SearchModel::matches(e, words) || m_settings.hiddenCalendars().contains(e.calendarId))
            continue;
        const QString key =
            e.calendarId + u'/' + (e.detached || e.seriesId.isEmpty() ? e.eventId : e.seriesId);
        const auto seen = found.constFind(key);
        const bool upcoming = e.end > now;
        if (seen == found.cend() || (upcoming ? (seen->end <= now || e.start < seen->start)
                                              : (seen->end <= now && e.start > seen->start)))
            found.insert(key, e);
    }
    showFound(tr("Found for %1").arg(text), found.values());
}

void TuiState::showFound(const QString &title, QList<Event> events)
{
    std::sort(events.begin(), events.end(),
              [](const Event &a, const Event &b) { return a.start < b.start; });
    m_found = events;
    m_foundTitle = title;
    // Nearest to now first in view.
    m_foundSelected = m_found.isEmpty() ? -1 : 0;
    for (int i = 0; i < m_found.size(); ++i) {
        if (m_found.at(i).end > m_now()) {
            m_foundSelected = i;
            break;
        }
    }
    m_mode = Mode::Found;
    Q_EMIT changed();
}

void TuiState::jumpTo(const Event &event)
{
    m_mode = Mode::Browse;
    m_view = View::Day;
    m_day = m_settings.times()->date(event.start).date();
    reload();
    for (int i = 0; i < m_events.size(); ++i) {
        if (m_events.at(i).eventId == event.eventId)
            m_selected = i;
    }
    Q_EMIT changed();
}

void TuiState::act(const std::function<void(Commands &, const std::function<void(int)> &)> &run)
{
    // The commands write their mistakes as the CLI would; the TUI shows them.
    auto out = std::make_shared<QString>();
    auto err = std::make_shared<QString>();
    auto outStream = std::make_shared<QTextStream>(out.get());
    auto errStream = std::make_shared<QTextStream>(err.get());
    auto commands = std::make_shared<Commands>(*m_source, m_settings, *outStream, *errStream,
                                               m_now(), m_settings.times()->zone());
    run(*commands, [this, commands, out, err, outStream, errStream](int code) {
        errStream->flush();
        if (code != 0) {
            QString why = err->trimmed();
            why.remove(u"callie: "_s);
            say(why.isEmpty() ? tr("That did not work.") : why, true);
        }
    });
    Q_EMIT changed();
}

void TuiState::say(const QString &message, bool error)
{
    m_message = message;
    m_messageIsError = error;
    // Gone once the change can no longer be taken back, or a moment after an error.
    QTimer::singleShot(std::chrono::seconds(error ? 5 : QueuedSource::kHoldSecs), this,
                       [this, message] {
                           if (m_message == message) {
                               m_message.clear();
                               Q_EMIT changed();
                           }
                       });
    Q_EMIT changed();
}

QList<CalendarInfo> TuiState::calendars() const
{
    return m_source ? m_source->calendars() : QList<CalendarInfo>();
}

bool TuiState::calendarShown(const CalendarInfo &calendar) const
{
    return m_settings.isShown(calendar);
}

std::optional<Event> TuiState::current() const
{
    if (m_selected < 0 || m_selected >= m_events.size())
        return std::nullopt;
    return m_events.at(m_selected);
}

QString TuiState::syncStatus() const
{
    if (!m_source)
        return {};
    if (m_source->syncing())
        return tr("Syncing...");
    if (!m_source->lastError().isEmpty())
        return m_source->lastError();
    const QDateTime synced = m_source->lastSynced();
    return synced.isValid() ? tr("Updated %1").arg(m_settings.times()->time(synced))
                            : tr("Not synced yet");
}

bool TuiState::syncFailed() const
{
    return m_source && !m_source->syncing() && !m_source->lastError().isEmpty();
}

} // namespace callie::cli
