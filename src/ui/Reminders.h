#pragma once

#include "callie/Event.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>

namespace callie {

class NotificationServer;
class ReminderScheduler;
class Settings;
class Times;

/// Turns due reminders into desktop notifications and answers their buttons:
/// open the event in Callie, join its call, or snooze for five minutes.
class Reminders : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    struct Text
    {
        QString title;
        QString body;
    };

    static Reminders *instance();
    static Reminders *create(QQmlEngine *, QJSEngine *);

    /// Starts notifying. Until then nothing is shown, as in tests and screenshots.
    void setup(ReminderScheduler *scheduler, NotificationServer *server, Settings *settings);

    /// What a notification about `event` says at `now`.
    [[nodiscard]] static Text describe(const Event &event, const QDateTime &now,
                                       const Times &times);

    static constexpr int kSnoozeMinutes = 5;

Q_SIGNALS:
    /// Asks the window to show `day` (a local midnight) and the event there
    /// with this uid that starts at `start`.
    void openRequested(const QDateTime &day, const QString &uid, const QDateTime &start);

private:
    explicit Reminders(QObject *parent);

    void applySettings();
    void notify(const Event &event);
    void onAction(uint id, const QString &key);

    QPointer<ReminderScheduler> m_scheduler;
    QPointer<NotificationServer> m_server;
    QPointer<Settings> m_settings;
    QHash<uint, Event> m_shown;
};

} // namespace callie
