#include "callie/EventModel.h"

#include "callie/SampleSource.h"

#include <algorithm>

namespace callie {

EventModel::EventModel(QObject *parent) : QAbstractListModel(parent)
{
    // TODO(core): replace with the real source registry once the backends land.
    setSource(new SampleSource(this));
}

void EventModel::setSource(CalendarSource *source)
{
    if (m_source == source)
        return;
    if (m_source)
        disconnect(m_source, nullptr, this, nullptr);
    m_source = source;
    if (m_source)
        connect(m_source, &CalendarSource::changed, this, &EventModel::reload);
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
    endResetModel();
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
    };
}

} // namespace callie
