#pragma once

#include "CalendarSource.h"
#include "Event.h"

#include <QAbstractListModel>
#include <QDate>
#include <QHash>
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
    /// The IANA zone days and times are shown in; empty follows the system.
    Q_PROPERTY(QString timeZoneId READ timeZoneId WRITE setTimeZoneId NOTIFY timeZoneChanged)
    /// Calendars left out, by CalendarInfo::id.
    Q_PROPERTY(QStringList hiddenCalendars READ hiddenCalendars WRITE setHiddenCalendars NOTIFY
                   filterChanged)
    Q_PROPERTY(bool showDeclined READ showDeclined WRITE setShowDeclined NOTIFY filterChanged)
    /// Short events are drawn at least this long, so lanes treat them as
    /// lasting this long; a 15-minute event then sits beside the next one.
    Q_PROPERTY(
        int minimumMinutes READ minimumMinutes WRITE setMinimumMinutes NOTIFY minimumMinutesChanged)
    /// How timing() writes clock times.
    Q_PROPERTY(bool use24Hour MEMBER m_use24Hour NOTIFY use24HourChanged)
    /// Rows the all-day strip needs so that no two all-day events overlap.
    Q_PROPERTY(int allDayRows READ allDayRows NOTIFY allDayRowsChanged)
    /// Goes up whenever the rows are replaced.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    /// The source's shown calendars as {id, name, color, account} maps, for the
    /// sidebar. Hidden calendars stay listed so they can be shown again.
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
    [[nodiscard]] QString timeZoneId() const;
    void setTimeZoneId(const QString &id);

    [[nodiscard]] QStringList hiddenCalendars() const { return m_hiddenCalendars; }
    void setHiddenCalendars(const QStringList &ids);

    [[nodiscard]] int minimumMinutes() const { return m_minimumMinutes; }
    void setMinimumMinutes(int minutes);

    [[nodiscard]] bool showDeclined() const { return m_showDeclined; }
    void setShowDeclined(bool show);

    [[nodiscard]] int revision() const { return m_revision; }
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

    /// The events touching day `dayIndex` of the range, all-day ones first and
    /// the rest by start, each as a map of this model's role names. For views
    /// that list a day's events rather than place them on a grid. Call it as
    /// `model.eventsOn(day, model.revision)`: QML cannot see what a C++ call
    /// reads, so passing the revision is what makes the binding update.
    Q_INVOKABLE QVariantList eventsOn(int dayIndex, int revision = 0) const;

    [[nodiscard]] QVariantList calendars() const { return m_calendars; }

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void sourceChanged();
    void rangeChanged();
    void timeZoneChanged();
    void filterChanged();
    void revisionChanged();
    void use24HourChanged();
    void minimumMinutesChanged();
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
    void assignLanes(QList<Event> &events) const;
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
    int m_minimumMinutes = 0;
    QList<Event> m_events;
    /// Descriptions as plain text, worked out on first read: parsing HTML is
    /// slow, and month and agenda read every row on each reset.
    mutable QHash<int, QString> m_plainDescriptions;
    /// Counts reloads, so a slow load that a newer one overtook is dropped.
    quint64 m_generation = 0;
    int m_allDayRows = 0;
    int m_revision = 0;
    QVariantList m_calendars;
    QHash<QString, QString> m_calendarNames;
};

} // namespace callie
