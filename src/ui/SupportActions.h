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
    /// Opens a new GitHub issue with the report filled in.
    Q_INVOKABLE void reportBug();

Q_SIGNALS:
    void copied();

private:
    void collect(std::function<void(const QString &)> done);

    KeychainTokenStore m_tokens;
};

} // namespace callie
