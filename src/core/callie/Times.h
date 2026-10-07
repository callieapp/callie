#pragma once

#include <QDateTime>
#include <QObject>
#include <QTimeZone>

namespace callie {

/// Formats and places times for one zone and clock format. Settings swaps in
/// a new one whenever either changes, so a QML binding that calls these
/// re-evaluates through Settings.times instead of going stale.
class Times : public QObject
{
    Q_OBJECT

public:
    Times(const QTimeZone &zone, bool use24Hour, QObject *parent = nullptr);

    [[nodiscard]] QTimeZone zone() const { return m_zone; }
    [[nodiscard]] bool use24Hour() const { return m_use24Hour; }

    /// A clock time: "14:30" or "2:30 PM".
    Q_INVOKABLE QString time(const QDateTime &time) const;
    /// An hour label for the grid: "14:00" or "2 PM".
    Q_INVOKABLE QString hour(int hour) const;
    /// The calendar date of `time` in the zone, as that day's local midnight:
    /// a QDate would reach QML as UTC midnight, a day early in the west.
    Q_INVOKABLE QDateTime date(const QDateTime &time) const;
    /// The wall-clock time `minutes` past midnight on `day` (a local midnight,
    /// as date() returns) in the zone; 1440 is the next midnight.
    Q_INVOKABLE QDateTime at(const QDateTime &day, int minutes) const;
    /// Minutes since midnight of `time` in the zone.
    Q_INVOKABLE int minutesIntoDay(const QDateTime &time) const;
    /// `moment` moved `days` and `minutes` on the wall clock of the zone, so
    /// a drag across a daylight saving change keeps the time it shows.
    Q_INVOKABLE QDateTime shifted(const QDateTime &moment, int days, int minutes) const;

    /// The same move in any zone, by seconds.
    [[nodiscard]] static QDateTime shiftWallClock(const QDateTime &moment, const QTimeZone &zone,
                                                  int days, qint64 seconds);

private:
    QTimeZone m_zone;
    bool m_use24Hour;
};

} // namespace callie
