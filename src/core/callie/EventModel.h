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
    /// The zone days and times are shown in.
    Q_PROPERTY(QTimeZone timeZone READ timeZone WRITE setTimeZone NOTIFY timeZoneChanged)
    /// Calendars left out, by CalendarInfo::id.
    Q_PROPERTY(QStringList hiddenCalendars READ hiddenCalendars WRITE setHiddenCalendars NOTIFY
                   filterChanged)
    Q_PROPERTY(bool showDeclined READ showDeclined WRITE setShowDeclined NOTIFY filterChanged)
    /// How timing() writes clock times.
    Q_PROPERTY(bool use24Hour MEMBER m_use24Hour NOTIFY use24HourChanged)
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
        DeclinedRole,
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

    [[nodiscard]] QTimeZone timeZone() const { return m_tz; }
    void setTimeZone(const QTimeZone &zone);

    [[nodiscard]] QStringList hiddenCalendars() const { return m_hiddenCalendars; }
    void setHiddenCalendars(const QStringList &ids);

    [[nodiscard]] bool showDeclined() const { return m_showDeclined; }
    void setShowDeclined(bool show);

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
    void timeZoneChanged();
    void filterChanged();
    void use24HourChanged();
    void allDayRowsChanged();
    void calendarsChanged();

private:
    /// Re-reads the shown range, keeping the rows until the answer arrives.
    void reload();
    /// Re-reads after the range changed, when the rows no longer fit it.
    void reloadRange();
    void load(bool rangeChanged);
    void apply(SourceSnapshot snapshot);
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
    QStringList m_hiddenCalendars;
    bool m_showDeclined = true;
    bool m_use24Hour = true;
    QList<Event> m_events;
    /// Counts reloads, so a slow load that a newer one overtook is dropped.
    quint64 m_generation = 0;
    int m_allDayRows = 0;
    QVariantList m_calendars;
    QHash<QString, QString> m_calendarNames;
};

} // namespace callie
