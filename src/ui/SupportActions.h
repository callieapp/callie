#pragma once

#include "callie/TokenStore.h"

#include <QObject>
#include <QQmlEngine>

#include <functional>

namespace callie {

/// The title bar's help menu: debug info, the logs folder and bug reports.
class SupportActions : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Support)
    QML_SINGLETON

public:
    using QObject::QObject;

    /// Puts the `callie doctor` report on the clipboard, then emits copied().
    Q_INVOKABLE void copyDebugInfo();
    Q_INVOKABLE void openLogs();
    /// Opens the folder holding the settings ("config"), the change queue
    /// ("data") or the event cache and contacts ("cache").
    Q_INVOKABLE void openFolder(const QString &which);
    /// Puts `text` on the clipboard, then emits copied().
    Q_INVOKABLE void copyText(const QString &text);
    /// Opens a new GitHub issue with the report filled in.
    Q_INVOKABLE void reportBug();

Q_SIGNALS:
    void copied();

private:
    void collect(std::function<void(const QString &)> done);

    KeychainTokenStore m_tokens;
};

} // namespace callie
