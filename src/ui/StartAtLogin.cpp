#include "StartAtLogin.h"

#include "callie/Autostart.h"
#include "callie/Identity.h"
#include "callie/Settings.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>

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
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.Background"), QStringLiteral("RequestBackground"));
    const QVariantMap options{
        {QStringLiteral("reason"),
         tr("Keeps %1 running for event reminders").arg(identity::displayName())},
        {QStringLiteral("autostart"), autostart},
        {QStringLiteral("commandline"),
         QStringList{QStringLiteral("callie-gui"),
                     QStringLiteral("--") + QString::fromLatin1(Autostart::kBackgroundOption)}},
        {QStringLiteral("dbus-activatable"), false},
    };
    call << QString() << options;
    QDBusConnection::sessionBus().asyncCall(call);
}

} // namespace callie
