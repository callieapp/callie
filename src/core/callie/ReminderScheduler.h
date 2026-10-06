#pragma once

#include "CalendarSource.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>

#include <functional>

namespace callie {

/// Watches a source for upcoming events and says when each reminder is due,
/// once. Events keep their source's reminders; others get `defaultMinutes`.
class ReminderScheduler : public QObject
{
    Q_OBJECT

public:
    using Now = std::function<QDateTime()>;

    explicit ReminderScheduler(QObject *parent = nullptr);

    void setSource(CalendarSource *source);
    /// Replaces the wall clock, for tests and `--now`.
    void setNow(Now now);
    /// Minutes before events without reminders of their own; negative for none.
    void setDefaultMinutes(int minutes);
    /// Calendars the user hid, which never remind.
    void setHiddenCalendars(const QStringList &calendars);
    /// Starts or stops watching. Reminders that fall due while stopped are
    /// skipped, not caught up on.
    void setEnabled(bool enabled);
    [[nodiscard]] bool isEnabled() const { return m_enabled; }

    /// Reminds about `event` again in `minutes`.
    void snooze(const Event &event, int minutes);

    /// Says which reminders fell due since the last check, then waits for the
    /// next one. Runs by itself; public so tests can step the clock.
    void check();

    /// How far ahead events are read. Reminders set further ahead than this
    /// are not seen in time.
    static constexpr qint64 kLookAheadDays = 8;

Q_SIGNALS:
    /// `minutesBefore` is how long before the start the reminder was set for,
    /// or -1 for a snoozed one.
    void due(const callie::Event &event, int minutesBefore);

private:
    struct Snoozed
    {
        Event event;
        QDateTime at;
    };

    [[nodiscard]] QList<int> remindersFor(const Event &event) const;
    void schedule(const QDateTime &now, const QDateTime &next);

    QPointer<CalendarSource> m_source;
    Now m_now;
    int m_defaultMinutes = 10;
    QStringList m_hidden;
    bool m_enabled = false;
    QDateTime m_lastCheck;
    /// Reminders already said, by key, with when they fell due.
    QHash<QString, QDateTime> m_said;
    QList<Snoozed> m_snoozed;
    QTimer m_timer;
};

} // namespace callie
