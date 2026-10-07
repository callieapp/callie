#pragma once

#include <QString>

namespace callie {

/// Starting Callie when the user logs in, through an XDG autostart entry in
/// `$XDG_CONFIG_HOME/autostart`, which every Linux desktop reads. The entry's
/// presence is the setting, so turning it off elsewhere is respected.
class Autostart
{
public:
    /// `program` is what the entry runs, in the background; `appId` names the
    /// entry and its icon. An empty `path` is the default one.
    Autostart(QString appId, QString program, QString path = {});

    /// `$XDG_CONFIG_HOME/autostart/<appId>.desktop`
    [[nodiscard]] static QString defaultPath(const QString &appId);

    [[nodiscard]] bool isEnabled() const;
    /// Writes or removes the entry. False, with errorString() set, on failure.
    bool setEnabled(bool enabled);
    [[nodiscard]] QString errorString() const { return m_error; }

    /// The option that starts Callie with its window hidden.
    static constexpr const char *kBackgroundOption = "background";

private:
    QString m_appId;
    QString m_program;
    QString m_path;
    QString m_error;
};

} // namespace callie
