#pragma once

#include "CalendarSource.h"

#include <QColor>
#include <QDateTime>
#include <QObject>
#include <QPointer>

namespace callie {

/// What the sidebar says about today: a greeting, the date, and the next event
/// still to start. QML sets `now` from its clock and `source` from the window.
class TodayModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(callie::CalendarSource *source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(QDateTime now READ now WRITE setNow NOTIFY changed)
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

    QPointer<CalendarSource> m_source;
    QDateTime m_now;
    Event m_next;
    QString m_nextCalendar;
};

} // namespace callie
