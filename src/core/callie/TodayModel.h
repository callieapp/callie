#pragma once

#include "CalendarSource.h"

#include <QColor>
#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QTimeZone>

namespace callie {

/// What the sidebar says about today: a greeting, the date, and the next event
/// still to start. QML sets `now` from its clock and `source` from the window.
class TodayModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(callie::CalendarSource *source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(QDateTime now READ now WRITE setNow NOTIFY changed)
    /// The IANA zone "today" and the times are in; empty follows the system.
    Q_PROPERTY(QString timeZoneId READ timeZoneId WRITE setTimeZoneId NOTIFY changed)
    Q_PROPERTY(bool use24Hour READ use24Hour WRITE setUse24Hour NOTIFY changed)
    /// Calendars the user hid, whose events are never up next.
    Q_PROPERTY(
        QStringList hiddenCalendars READ hiddenCalendars WRITE setHiddenCalendars NOTIFY changed)
    Q_PROPERTY(QString greeting READ greeting NOTIFY changed)
    Q_PROPERTY(QString dateLabel READ dateLabel NOTIFY changed)
    Q_PROPERTY(bool hasNext READ hasNext NOTIFY changed)
    Q_PROPERTY(QString nextLabel READ nextLabel NOTIFY changed)
    Q_PROPERTY(QString nextTitle READ nextTitle NOTIFY changed)
    Q_PROPERTY(QString nextDetail READ nextDetail NOTIFY changed)
    Q_PROPERTY(QColor nextColor READ nextColor NOTIFY changed)

public:
    explicit TodayModel(QObject *parent = nullptr);

    [[nodiscard]] CalendarSource *source() const { return m_source; }
    void setSource(CalendarSource *source);
    [[nodiscard]] QDateTime now() const { return m_now; }
    void setNow(const QDateTime &now);
    [[nodiscard]] QTimeZone timeZone() const { return m_zone; }
    void setTimeZone(const QTimeZone &zone);
    [[nodiscard]] QString timeZoneId() const;
    void setTimeZoneId(const QString &id);
    [[nodiscard]] bool use24Hour() const { return m_use24Hour; }
    void setUse24Hour(bool use24Hour);
    [[nodiscard]] QStringList hiddenCalendars() const { return m_hiddenCalendars; }
    void setHiddenCalendars(const QStringList &ids);

    [[nodiscard]] QString greeting() const;
    [[nodiscard]] QString dateLabel() const;
    [[nodiscard]] bool hasNext() const { return m_next.isValid(); }
    /// "Up next, in 20 min" within the hour, "Up next at 16:00" after that.
    [[nodiscard]] QString nextLabel() const;
    [[nodiscard]] QString nextTitle() const { return m_next.summary; }
    /// "11:00 to 11:30, Room 2", naming the calendar when there is no location.
    [[nodiscard]] QString nextDetail() const;
    [[nodiscard]] QColor nextColor() const { return m_next.color; }

Q_SIGNALS:
    void changed();

private:
    void refresh();
    void apply(const SourceSnapshot &snapshot);

    QPointer<CalendarSource> m_source;
    QDateTime m_now;
    QTimeZone m_zone = QTimeZone::systemTimeZone();
    bool m_use24Hour = true;
    QStringList m_hiddenCalendars;
    Event m_next;
    QString m_nextCalendar;
    /// Counts refreshes, so a slow read that a newer one overtook is dropped.
    quint64 m_generation = 0;
};

} // namespace callie
