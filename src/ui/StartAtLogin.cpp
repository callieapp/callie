#include "StartAtLogin.h"

#include "callie/Autostart.h"
#include "callie/Identity.h"
#include "callie/Settings.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QRandomGenerator>

using namespace Qt::StringLiterals;

namespace callie {

StartAtLogin::StartAtLogin(QObject *parent) : QObject(parent) {}

StartAtLogin *StartAtLogin::instance()
{
    static auto *startAtLogin = new StartAtLogin(QCoreApplication::instance());
    return startAtLogin;
}

StartAtLogin *StartAtLogin::create(QQmlEngine *, QJSEngine *)
{
    StartAtLogin *startAtLogin = instance();
    QJSEngine::setObjectOwnership(startAtLogin, QJSEngine::CppOwnership);
    return startAtLogin;
}

void StartAtLogin::setup(Autostart *autostart, Settings *settings)
{
    if (m_settings)
        disconnect(m_settings, nullptr, this, nullptr);
    m_autostart = autostart;
    m_settings = settings;
    if (m_settings) {
        connect(m_settings, &Settings::keepRunningChanged, this, [this] {
            if (!m_settings->keepRunning())
                setEnabled(false);
        });
    }
    Q_EMIT changed();
}

bool StartAtLogin::enabled() const
{
    return m_autostart && m_autostart->isEnabled();
}

void StartAtLogin::setEnabled(bool enabled)
{
    if (!m_autostart)
        return;
    if (enabled == this->enabled()) {
        // Already so, perhaps from the desktop's own settings: nothing has failed.
        if (!m_error.isEmpty()) {
            m_error.clear();
            Q_EMIT changed();
        }
        return;
    }
    m_error = m_autostart->setEnabled(enabled) ? QString() : m_autostart->errorString();
    // In a Flatpak the entry stays in the sandbox, as the record of the setting,
    // and the Background portal starts Callie at login.
    if (m_error.isEmpty() && identity::isFlatpak())
        requestBackground(enabled);
    Q_EMIT changed();
}

void StartAtLogin::requestBackground(bool autostart)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!m_request.isEmpty())
        bus.disconnect(m_portal, m_request, u"org.freedesktop.portal.Request"_s, u"Response"_s,
                       this, SLOT(backgroundAnswered(uint, QVariantMap)));
    // The request's path is known ahead, so its answer cannot come before Callie listens.
    const QString token = u"callie%1"_s.arg(QRandomGenerator::global()->generate());
    QString sender = bus.baseService().mid(1);
    sender.replace(u'.', u'_');
    m_request = u"/org/freedesktop/portal/desktop/request/%1/%2"_s.arg(sender, token);
    m_requestedAutostart = autostart;
    bus.connect(m_portal, m_request, u"org.freedesktop.portal.Request"_s, u"Response"_s, this,
                SLOT(backgroundAnswered(uint, QVariantMap)));

    QDBusMessage call = QDBusMessage::createMethodCall(
        m_portal, u"/org/freedesktop/portal/desktop"_s, u"org.freedesktop.portal.Background"_s,
        u"RequestBackground"_s);
    const QVariantMap options{
        {u"handle_token"_s, token},
        {u"reason"_s, tr("Keeps %1 running for event reminders").arg(identity::displayName())},
        {u"autostart"_s, autostart},
        {u"commandline"_s,
         QStringList{u"callie-gui"_s, u"--"_s + QString::fromLatin1(Autostart::kBackgroundOption)}},
        {u"dbus-activatable"_s, false},
    };
    call << QString() << options;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, autostart] {
        watcher->deleteLater();
        if (watcher->isError() && autostart)
            refused(tr("the desktop could not be asked: %1").arg(watcher->error().message()));
    });
}

void StartAtLogin::backgroundAnswered(uint response, const QVariantMap &results)
{
    QDBusConnection::sessionBus().disconnect(m_portal, m_request,
                                             u"org.freedesktop.portal.Request"_s, u"Response"_s,
                                             this, SLOT(backgroundAnswered(uint, QVariantMap)));
    m_request.clear();
    // 0 is yes; 1 is the user saying no, 2 anything else.
    if (m_requestedAutostart && (response != 0 || !results.value(u"autostart"_s).toBool()))
        refused(tr("the desktop did not allow %1 to start at login").arg(identity::displayName()));
}

void StartAtLogin::refused(const QString &why)
{
    if (m_autostart)
        m_autostart->setEnabled(false);
    m_error = why;
    Q_EMIT changed();
}

} // namespace callie
