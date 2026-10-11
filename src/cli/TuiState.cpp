#include "TuiState.h"

#include "callie/Settings.h"
#include "callie/Times.h"

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
    } else if (key == u"r") {
        if (m_source)
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
