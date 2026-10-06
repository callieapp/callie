#include "Reminders.h"

#include "NotificationServer.h"

#include "callie/ReminderScheduler.h"
#include "callie/Settings.h"
#include "callie/Times.h"

#include <QCoreApplication>
#include <QDesktopServices>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

bool canJoin(const QUrl &url)
{
    return url.isValid() && (url.scheme() == u"https"_s || url.scheme() == u"http"_s);
}

} // namespace

Reminders::Reminders(QObject *parent) : QObject(parent) {}

Reminders *Reminders::instance()
{
    static auto *reminders = new Reminders(QCoreApplication::instance());
    return reminders;
}

Reminders *Reminders::create(QQmlEngine *, QJSEngine *)
{
    Reminders *reminders = instance();
    QJSEngine::setObjectOwnership(reminders, QJSEngine::CppOwnership);
    return reminders;
}

void Reminders::setup(ReminderScheduler *scheduler, NotificationServer *server, Settings *settings)
{
    if (m_scheduler)
        disconnect(m_scheduler, nullptr, this, nullptr);
    if (m_server)
        disconnect(m_server, nullptr, this, nullptr);
    if (m_settings)
        disconnect(m_settings, nullptr, this, nullptr);
    m_scheduler = scheduler;
    m_server = server;
    m_settings = settings;
    m_shown.clear();

    connect(m_scheduler, &ReminderScheduler::due, this,
            [this](const Event &event) { notify(event); });
    connect(m_server, &NotificationServer::actionInvoked, this, &Reminders::onAction);
    connect(m_server, &NotificationServer::closed, this, [this](uint id) { m_shown.remove(id); });
    connect(m_settings, &Settings::notifyChanged, this, &Reminders::applySettings);
    connect(m_settings, &Settings::reminderMinutesChanged, this, &Reminders::applySettings);
    connect(m_settings, &Settings::hiddenCalendarsChanged, this, &Reminders::applySettings);
    connect(m_settings, &Settings::timeZoneChanged, this, &Reminders::applySettings);
    applySettings();
}

void Reminders::applySettings()
{
    m_scheduler->setHiddenCalendars(m_settings->hiddenCalendars());
    m_scheduler->setTimeZone(m_settings->timeZone());
    m_scheduler->setDefaultMinutes(m_settings->reminderMinutes());
    m_scheduler->setEnabled(m_settings->notify());
}

Reminders::Text Reminders::describe(const Event &event, const QDateTime &now, const Times &times)
{
    const QString title = event.summary.isEmpty() ? tr("(No title)") : event.summary;
    const QDateTime start = event.start.toTimeZone(times.zone());
    const qint64 seconds = now.secsTo(event.start);

    QString when;
    if (event.allDay) {
        when = start.date() == now.toTimeZone(times.zone()).date()
                   ? tr("Today")
                   : QLocale().toString(start.date(), u"dddd, MMMM d"_s);
    } else {
        const QString span = tr("%1 to %2").arg(times.time(event.start), times.time(event.end));
        if (seconds <= 30) {
            when = tr("Now, %1").arg(span);
        } else if (seconds < 90 * 60) {
            const int minutes = int((seconds + 59) / 60);
            when = tr("In %1, %2").arg(tr("%n min", nullptr, minutes), span);
        } else if (start.date() == now.toTimeZone(times.zone()).date()) {
            when = span;
        } else {
            when = tr("%1, %2").arg(QLocale().toString(start.date(), u"dddd"_s), span);
        }
    }
    return {title, event.location.isEmpty() ? when : when + u'\n' + event.location};
}

void Reminders::notify(const Event &event)
{
    if (!m_server || !m_settings || !m_scheduler)
        return;
    const Text text = describe(event, m_scheduler->now(), *m_settings->times());
    // "default" is a click on the notification; daemons show it as that, not
    // as one more button.
    QStringList actions{u"default"_s, tr("Open in Callie")};
    if (canJoin(event.conferenceUrl))
        actions << u"join"_s << tr("Join call");
    actions << u"snooze"_s << tr("Snooze %n min", nullptr, kSnoozeMinutes);
    m_server->show(text.title, text.body, actions, [guard = QPointer(this), event](uint id) {
        if (guard && id != 0)
            guard->m_shown.insert(id, event);
    });
}

void Reminders::onAction(uint id, const QString &key)
{
    const auto found = m_shown.constFind(id);
    if (found == m_shown.cend())
        return;
    const Event event = *found;
    if (key == u"join"_s && canJoin(event.conferenceUrl)) {
        QDesktopServices::openUrl(event.conferenceUrl);
    } else if (key == u"snooze"_s) {
        m_scheduler->snooze(event, kSnoozeMinutes);
    } else if (key == u"default"_s) {
        const Times &times = *m_settings->times();
        Q_EMIT openRequested(times.date(event.start), event.uid, event.start);
    } else {
        return;
    }
    m_server->close(id);
    m_shown.remove(id);
}

} // namespace callie
