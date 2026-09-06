#pragma once

#include "CalendarSource.h"
#include "Event.h"

#include <QAbstractListModel>
#include <QDate>
#include <QTimeZone>

namespace callie {

/// Flat list of expanded occurrences covering [rangeStart, rangeStart + dayCount).
/// Roles carry pre-computed offsets and lane assignments so that QML positions
/// events without doing date math.
class EventModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QDate rangeStart READ rangeStart WRITE setRangeStart NOTIFY rangeChanged)
    Q_PROPERTY(int dayCount READ dayCount WRITE setDayCount NOTIFY rangeChanged)

public:
    enum Role {
        UidRole = Qt::UserRole + 1,
        SummaryRole,
        LocationRole,
        ConferenceUrlRole,
        CalendarColorRole,
        AllDayRole,
        DayIndexRole,
        StartMinutesRole,
        DurationMinutesRole,
        StartRole,
        EndRole,
        LaneRole,
        LaneCountRole,
    };
    Q_ENUM(Role)

    explicit EventModel(QObject *parent = nullptr);

    void setSource(CalendarSource *source);

    [[nodiscard]] QDate rangeStart() const { return m_rangeStart; }
    void setRangeStart(QDate date);

    [[nodiscard]] int dayCount() const { return m_dayCount; }
    void setDayCount(int days);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void rangeChanged();

private:
    void reload();
    /// Assigns lane/laneCount to every timed event in `events`, per day.
    static void assignLanes(QList<Event> &events);

    CalendarSource *m_source = nullptr;
    QDate m_rangeStart = QDate::currentDate();
    int m_dayCount = 7;
    QTimeZone m_tz = QTimeZone::systemTimeZone();
    QList<Event> m_events;
};

} // namespace callie
