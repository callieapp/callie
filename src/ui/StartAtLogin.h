#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>

namespace callie {

class Autostart;
class Settings;

/// The "start when I log in" setting. It only makes sense while Callie keeps
/// running after its window closes, so turning that off turns this off too.
class StartAtLogin : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(StartAtLogin)
    QML_SINGLETON
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    /// False for sample data and screenshots, which never touch the session.
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    static StartAtLogin *instance();
    static StartAtLogin *create(QQmlEngine *, QJSEngine *);

    void setup(Autostart *autostart, Settings *settings);

    [[nodiscard]] bool enabled() const;
    void setEnabled(bool enabled);
    [[nodiscard]] bool available() const { return m_autostart != nullptr; }
    [[nodiscard]] QString error() const { return m_error; }

Q_SIGNALS:
    void changed();

private:
    explicit StartAtLogin(QObject *parent);

    Autostart *m_autostart = nullptr;
    QPointer<Settings> m_settings;
    QString m_error;
};

} // namespace callie
