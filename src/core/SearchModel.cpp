#include "callie/SearchModel.h"

#include <QHash>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

// What was read is searched again as the user types, and read afresh once it
// is this old or the source has changed.
constexpr qint64 kRereadSecs = 10 * 60;

} // namespace

SearchModel::SearchModel(QObject *parent) : QAbstractListModel(parent) {}

void SearchModel::setSource(CalendarSource *source)
{
    if (m_source == source)
        return;
    if (m_source)
        disconnect(m_source, nullptr, this, nullptr);
    m_source = source;
    if (m_source) {
        connect(m_source, &CalendarSource::changed, this, [this] {
            m_stale = true;
            if (!m_query.trimmed().isEmpty())
                ensureLoaded();
        });
    }
    m_stale = true;
    m_pool.clear();
    Q_EMIT sourceChanged();
    filter();
}

void SearchModel::setQuery(const QString &query)
{
    if (m_query == query)
        return;
    m_query = query;
    Q_EMIT queryChanged();
    if (!m_query.trimmed().isEmpty())
        ensureLoaded();
    filter();
}

void SearchModel::setNow(const QDateTime &now)
{
    if (m_now == now)
        return;
    m_now = now;
    Q_EMIT nowChanged();
    filter();
}

void SearchModel::setHiddenCalendars(const QStringList &calendars)
{
    if (m_hidden == calendars)
        return;
    m_hidden = calendars;
    Q_EMIT hiddenCalendarsChanged();
    filter();
}

void SearchModel::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    Q_EMIT busyChanged();
}

void SearchModel::ensureLoaded()
{
    if (!m_source || !m_now.isValid())
        return;
    const bool fresh = !m_stale && m_loadedAt.isValid() && m_loadedAt.secsTo(m_now) < kRereadSecs;
    if (fresh || m_busy)
        return;
    const quint64 generation = ++m_generation;
    m_stale = false;
    m_loadedAt = m_now;
    QFuture<SourceSnapshot> future =
        m_source->load(m_now.addDays(-kDaysAround), m_now.addDays(kDaysAround), m_now.timeZone());
    if (future.isFinished()) {
        m_pool = future.result().events;
        filter();
        return;
    }
    setBusy(true);
    future.then(this, [this, generation](const SourceSnapshot &snapshot) {
        if (generation != m_generation)
            return;
        m_pool = snapshot.events;
        setBusy(false);
        filter();
        // The source changed while it was being read.
        if (m_stale && !m_query.trimmed().isEmpty())
            ensureLoaded();
    });
}

bool SearchModel::matches(const Event &event, const QStringList &words)
{
    QString haystack = event.summary + u'\n' + event.location + u'\n' + event.description;
    for (const Guest &guest : event.guests)
        haystack += u'\n' + guest.name + u'\n' + guest.email;
    for (const QString &word : words) {
        if (!haystack.contains(word, Qt::CaseInsensitive))
            return false;
    }
    return true;
}

void SearchModel::filter()
{
    const QStringList words = m_query.split(u' ', Qt::SkipEmptyParts);
    QList<Event> results;
    if (!words.isEmpty()) {
        // One result per event: the next occurrence, else the latest one.
        QHash<QString, qsizetype> chosen;
        for (const Event &event : std::as_const(m_pool)) {
            if (m_hidden.contains(event.calendarId) || !matches(event, words))
                continue;
            const QString key = event.calendarId + u'|' + event.uid;
            const auto found = chosen.constFind(key);
            if (found == chosen.cend()) {
                chosen.insert(key, results.size());
                results.append(event);
                continue;
            }
            Event &kept = results[*found];
            const bool keptAhead = kept.end > m_now;
            const bool ahead = event.end > m_now;
            if (ahead && (!keptAhead || event.start < kept.start))
                kept = event;
            else if (!ahead && !keptAhead && event.start > kept.start)
                kept = event;
        }
        std::sort(results.begin(), results.end(), [this](const Event &a, const Event &b) {
            const bool aAhead = a.end > m_now, bAhead = b.end > m_now;
            if (aAhead != bAhead)
                return aAhead;
            return aAhead ? a.start < b.start : a.start > b.start;
        });
        if (results.size() > kMaxResults)
            results.resize(kMaxResults);
    }

    const bool counted = results.size() != m_results.size();
    beginResetModel();
    m_results = results;
    endResetModel();
    if (counted)
        Q_EMIT countChanged();
}

int SearchModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_results.size());
}

QVariant SearchModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_results.size())
        return {};
    const Event &e = m_results.at(index.row());
    switch (role) {
    case SummaryRole: return e.summary;
    case StartRole: return e.start;
    case EndRole: return e.end;
    case AllDayRole: return e.allDay;
    case LocationRole: return e.location;
    case CalendarColorRole: return e.color;
    case UidRole: return e.uid;
    case UpcomingRole: return e.end > m_now;
    default: return {};
    }
}

QHash<int, QByteArray> SearchModel::roleNames() const
{
    return {{SummaryRole, "summary"},   {StartRole, "start"},
            {EndRole, "end"},           {AllDayRole, "allDay"},
            {LocationRole, "location"}, {CalendarColorRole, "calendarColor"},
            {UidRole, "uid"},           {UpcomingRole, "upcoming"}};
}

} // namespace callie
