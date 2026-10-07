#include "callie/InvitesModel.h"

#include <QSet>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

// Reading three months of events is not free, so the clock ticking only
// filters what was read, and a fresh read waits this long.
constexpr qint64 kRereadSecs = 15 * 60;

} // namespace

InvitesModel::InvitesModel(QObject *parent) : QAbstractListModel(parent) {}

void InvitesModel::setSource(CalendarSource *source)
{
    if (m_source == source)
        return;
    if (m_source)
        disconnect(m_source, nullptr, this, nullptr);
    m_source = source;
    if (m_source)
        connect(m_source, &CalendarSource::changed, this, &InvitesModel::reload);
    Q_EMIT sourceChanged();
    reload();
}

void InvitesModel::setNow(const QDateTime &now)
{
    if (m_now == now)
        return;
    m_now = now;
    Q_EMIT nowChanged();
    if (!m_loadedAt.isValid() || m_loadedAt.secsTo(now) >= kRereadSecs || now < m_loadedAt)
        reload();
    else
        apply(m_loaded);
}

void InvitesModel::setHiddenCalendars(const QStringList &calendars)
{
    if (m_hidden == calendars)
        return;
    m_hidden = calendars;
    Q_EMIT hiddenCalendarsChanged();
    apply(m_loaded);
}

void InvitesModel::reload()
{
    const quint64 generation = ++m_generation;
    if (!m_source || !m_now.isValid()) {
        apply({});
        return;
    }
    m_loadedAt = m_now;
    QFuture<SourceSnapshot> future =
        m_source->load(m_now, m_now.addDays(kDaysAhead), m_now.timeZone());
    if (future.isFinished()) {
        m_loaded = future.result().events;
        apply(m_loaded);
        return;
    }
    future.then(this, [this, generation](const SourceSnapshot &snapshot) {
        if (generation != m_generation)
            return;
        m_loaded = snapshot.events;
        apply(m_loaded);
    });
}

void InvitesModel::apply(const QList<Event> &events)
{
    QList<Event> invites;
    QSet<QString> series;
    for (const Event &event : events) {
        if (event.responseStatus != u"needsAction"_s || !event.canRespond ||
            m_hidden.contains(event.calendarId) || event.end <= m_now)
            continue;
        invites.append(event);
    }
    std::sort(invites.begin(), invites.end(),
              [](const Event &a, const Event &b) { return a.start < b.start; });
    // Only the next occurrence of each series.
    invites.removeIf([&series](const Event &event) {
        if (event.seriesId.isEmpty())
            return false;
        const QString key = event.calendarId + u'|' + event.seriesId;
        if (series.contains(key))
            return true;
        series.insert(key);
        return false;
    });

    const bool counted = invites.size() != m_invites.size();
    beginResetModel();
    m_invites = invites;
    endResetModel();
    if (counted)
        Q_EMIT countChanged();
}

QVariantMap InvitesModel::inviteAt(int row) const
{
    if (row < 0 || row >= m_invites.size())
        return {};
    const Event &e = m_invites.at(row);
    return {{u"uid"_s, e.uid},
            {u"summary"_s, e.summary},
            {u"calendarId"_s, e.calendarId},
            {u"eventId"_s, e.eventId},
            {u"seriesId"_s, e.seriesId},
            {u"recurrenceId"_s, e.recurrenceId},
            {u"allDay"_s, e.allDay},
            {u"start"_s, e.start},
            {u"end"_s, e.end},
            {u"attendees"_s, e.attendees}};
}

int InvitesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_invites.size());
}

QVariant InvitesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_invites.size())
        return {};
    const Event &e = m_invites.at(index.row());
    switch (role) {
    case SummaryRole: return e.summary;
    case StartRole: return e.start;
    case EndRole: return e.end;
    case AllDayRole: return e.allDay;
    case CalendarColorRole: return e.color;
    case RepeatsRole: return !e.seriesId.isEmpty();
    default: return {};
    }
}

QHash<int, QByteArray> InvitesModel::roleNames() const
{
    return {{SummaryRole, "summary"},
            {StartRole, "start"},
            {EndRole, "end"},
            {AllDayRole, "allDay"},
            {CalendarColorRole, "calendarColor"},
            {RepeatsRole, "repeats"}};
}

} // namespace callie
