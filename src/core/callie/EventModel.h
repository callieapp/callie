#pragma once

#include "CalendarSource.h"
#include "Event.h"

#include <QAbstractListModel>
#include <QDate>
#include <QTimeZone>
#include <QUrl>

#include <utility>

namespace callie {

/// Flat list of expanded occurrences covering [rangeStart, rangeStart + dayCount).
/// Roles carry pre-computed offsets and lane assignments so that QML positions
/// events without doing date math.
class EventModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(callie::CalendarSource *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QDate rangeStart READ rangeStart WRITE setRangeStart NOTIFY rangeChanged)
    Q_PROPERTY(int dayCount READ dayCount WRITE setDayCount NOTIFY rangeChanged)
    /// Rows the all-day strip needs so that no two all-day events overlap.
    Q_PROPERTY(int allDayRows READ allDayRows NOTIFY allDayRowsChanged)
    /// The source's shown calendars as {name, color} maps, for the sidebar.
    Q_PROPERTY(QVariantList calendars READ calendars NOTIFY calendarsChanged)

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
        FirstDayRole,
        DaySpanRole,
        CalendarNameRole,
        DescriptionRole,
    };
    Q_ENUM(Role)

    explicit EventModel(QObject *parent = nullptr);

    [[nodiscard]] CalendarSource *source() const { return m_source; }
    void setSource(CalendarSource *source);

    [[nodiscard]] QDate rangeStart() const { return m_rangeStart; }
    void setRangeStart(QDate date);

    [[nodiscard]] int dayCount() const { return m_dayCount; }
    void setDayCount(int days);

    [[nodiscard]] int allDayRows() const { return m_allDayRows; }

    /// Whether the user's locale treats `date` as a day off, for shading it.
    Q_INVOKABLE bool isDayOff(QDate date) const;

    /// Where an event stands at `now`: "Happening now, 50 min left", "Starts in
    /// 20 min", "Starts at 16:00" later today, "Ended" before now, and empty on
    /// another day, where the date says enough.
    Q_INVOKABLE QString timing(const QDateTime &start, const QDateTime &end,
                               const QDateTime &now) const;
    /// Which service a conference link joins: "zoom", "meet", "web" for any other
    /// http(s) link, or empty for a link Callie should not open.
    Q_INVOKABLE QString callService(const QUrl &url) const;

    [[nodiscard]] QVariantList calendars() const { return m_calendars; }

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void sourceChanged();
    void rangeChanged();
    void allDayRowsChanged();
    void calendarsChanged();

private:
    void reload();
    /// Assigns lane/laneCount to every timed event in `events`, per day.
    static void assignLanes(QList<Event> &events);
    /// Gives each all-day event a row in `lane` and returns the rows used.
    int assignAllDayRows(QList<Event> &events) const;
    /// The visible columns an event covers, as [first, first + span).
    [[nodiscard]] std::pair<int, int> visibleDays(const Event &event) const;

    CalendarSource *m_source = nullptr;
    QDate m_rangeStart = QDate::currentDate();
    int m_dayCount = 7;
    QTimeZone m_tz = QTimeZone::systemTimeZone();
    QList<Event> m_events;
    int m_allDayRows = 0;
    QVariantList m_calendars;
    QHash<QString, QString> m_calendarNames;
};

} // namespace callie
