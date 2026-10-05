#include "callie/EventModel.h"

#include <algorithm>

namespace callie {

EventModel::EventModel(QObject *parent) : QAbstractListModel(parent) {}

void EventModel::setSource(CalendarSource *source)
{
    if (m_source == source)
        return;
    if (m_source)
        disconnect(m_source, nullptr, this, nullptr);
    m_source = source;
    if (m_source)
        connect(m_source, &CalendarSource::changed, this, &EventModel::reload);
    Q_EMIT sourceChanged();
    reload();
}

void EventModel::setRangeStart(QDate date)
{
    if (m_rangeStart == date)
        return;
    m_rangeStart = date;
    Q_EMIT rangeChanged();
    reload();
}

void EventModel::setDayCount(int days)
{
    if (m_dayCount == days || days <= 0)
        return;
    m_dayCount = days;
    Q_EMIT rangeChanged();
    reload();
}

void EventModel::reload()
{
    beginResetModel();
    m_events.clear();
    if (m_source) {
        const QDateTime from(m_rangeStart, QTime(0, 0), m_tz);
        const QDateTime to(m_rangeStart.addDays(m_dayCount), QTime(0, 0), m_tz);
        m_events = m_source->eventsBetween(from, to, m_tz);
        assignLanes(m_events);
    }
    const int rows = assignAllDayRows(m_events);
    endResetModel();
    if (rows != m_allDayRows) {
        m_allDayRows = rows;
        Q_EMIT allDayRowsChanged();
    }

    QVariantList calendars;
    if (m_source) {
        for (const CalendarInfo &calendar : m_source->calendars()) {
            if (calendar.enabled)
                calendars.append(QVariantMap{{QStringLiteral("name"), calendar.displayName},
                                             {QStringLiteral("color"), calendar.color}});
        }
    }
    if (calendars != m_calendars) {
        m_calendars = calendars;
        Q_EMIT calendarsChanged();
    }
}

void EventModel::assignLanes(QList<Event> &events)
{
    // Sweep each day assigning the lowest free lane. A cluster ends at the first
    // gap, and every event in it then learns the cluster's final lane count.
    QMap<qint64, QList<Event *>> byDay;
    for (Event &e : events) {
        if (e.allDay)
            continue;
        byDay[e.start.date().toJulianDay()].append(&e);
    }

    for (QList<Event *> &day : byDay) {
        std::sort(day.begin(), day.end(), [](const Event *a, const Event *b) {
            return a->start == b->start ? a->end > b->end : a->start < b->start;
        });

        QList<Event *> cluster;
        QList<QDateTime> laneEnds;
        auto flush = [&] {
            for (Event *e : cluster)
                e->laneCount = laneEnds.size();
            cluster.clear();
            laneEnds.clear();
        };

        for (Event *e : day) {
            const bool overlapsCluster =
                std::any_of(laneEnds.cbegin(), laneEnds.cend(),
                            [e](const QDateTime &end) { return end > e->start; });
            if (!overlapsCluster)
                flush();

            int lane = 0;
            for (; lane < laneEnds.size(); ++lane) {
                if (laneEnds[lane] <= e->start)
                    break;
            }
            if (lane == laneEnds.size())
                laneEnds.append(e->end);
            else
                laneEnds[lane] = e->end;

            e->lane = lane;
            cluster.append(e);
        }
        flush();
    }
}

std::pair<int, int> EventModel::visibleDays(const Event &event) const
{
    const auto first = static_cast<int>(m_rangeStart.daysTo(event.start.date()));
    // An end at midnight belongs to the previous day.
    QDate endDay = event.end.date();
    if (event.end.time() == QTime(0, 0) && event.end > event.start)
        endDay = endDay.addDays(-1);
    const auto last = static_cast<int>(m_rangeStart.daysTo(endDay));
    const int clampedFirst = std::clamp(first, 0, m_dayCount - 1);
    const int clampedLast = std::clamp(last, clampedFirst, m_dayCount - 1);
    return {clampedFirst, clampedLast - clampedFirst + 1};
}

int EventModel::assignAllDayRows(QList<Event> &events) const
{
    QList<Event *> allDay;
    for (Event &e : events) {
        if (e.allDay)
            allDay.append(&e);
    }
    // Longer events first, so multi-day bars sit above single days.
    std::sort(allDay.begin(), allDay.end(), [this](const Event *a, const Event *b) {
        const auto [aFirst, aSpan] = visibleDays(*a);
        const auto [bFirst, bSpan] = visibleDays(*b);
        return aFirst == bFirst ? aSpan > bSpan : aFirst < bFirst;
    });

    QList<int> rowEnds;
    for (Event *e : allDay) {
        const auto [first, span] = visibleDays(*e);
        int row = 0;
        while (row < rowEnds.size() && rowEnds[row] > first)
            ++row;
        if (row == rowEnds.size())
            rowEnds.append(first + span);
        else
            rowEnds[row] = first + span;
        e->lane = row;
        e->laneCount = 1;
    }
    return int(rowEnds.size());
}

int EventModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_events.size();
}

QVariant EventModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_events.size())
        return {};

    const Event &e = m_events.at(index.row());
    switch (role) {
    case UidRole: return e.uid;
    case SummaryRole: return e.summary;
    case LocationRole: return e.location;
    case ConferenceUrlRole: return e.conferenceUrl;
    case CalendarColorRole: return e.color;
    case AllDayRole: return e.allDay;
    case StartRole: return e.start;
    case EndRole: return e.end;
    case LaneRole: return e.lane;
    case LaneCountRole: return e.laneCount;
    case DayIndexRole: return static_cast<int>(m_rangeStart.daysTo(e.start.date()));
    case StartMinutesRole: return e.start.time().hour() * 60 + e.start.time().minute();
    case DurationMinutesRole: return static_cast<int>(e.start.secsTo(e.end) / 60);
    case FirstDayRole: return visibleDays(e).first;
    case DaySpanRole: return visibleDays(e).second;
    default: return {};
    }
}

QHash<int, QByteArray> EventModel::roleNames() const
{
    return {
        {UidRole, "uid"},
        {SummaryRole, "summary"},
        {LocationRole, "location"},
        {ConferenceUrlRole, "conferenceUrl"},
        {CalendarColorRole, "calendarColor"},
        {AllDayRole, "allDay"},
        {DayIndexRole, "dayIndex"},
        {StartMinutesRole, "startMinutes"},
        {DurationMinutesRole, "durationMinutes"},
        {StartRole, "start"},
        {EndRole, "end"},
        {LaneRole, "lane"},
        {LaneCountRole, "laneCount"},
        {FirstDayRole, "firstDay"},
        {DaySpanRole, "daySpan"},
    };
}

} // namespace callie
