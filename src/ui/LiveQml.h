#pragma once

#include <QFileSystemWatcher>
#include <QList>
#include <QObject>
#include <QRect>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>

class QQmlApplicationEngine;

namespace callie {

/// Development only: serves QML modules from their source folders and reloads
/// the window when a .qml file is saved, so QML edits need no rebuild.
class LiveQml : public QObject
{
    Q_OBJECT

public:
    struct Module
    {
        QString uri;       ///< e.g. "Callie.Ui"
        QString sourceDir; ///< where its .qml files are edited
        QString buildDir;  ///< where the build wrote its qmldir
    };

    /// `load` creates the window; it runs again on every reload.
    LiveQml(QQmlApplicationEngine &engine, QList<Module> modules, std::function<void()> load,
            QObject *parent = nullptr);

    /// Puts the modules on the engine's import path ahead of the compiled ones
    /// and starts watching. Call before the first load().
    bool start();
    [[nodiscard]] QString errorString() const { return m_error; }

private:
    bool mirror(const Module &module);
    [[nodiscard]] QStringList qmlFiles() const;
    void onDirectoryChanged();
    void watchSources();
    void reload();

    QQmlApplicationEngine &m_engine;
    QList<Module> m_modules;
    std::function<void()> m_load;
    QTemporaryDir m_dir;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_error;
    QStringList m_knownFiles;
    /// Kept here, because a failed load leaves no window to read it from.
    QRect m_geometry;
};

} // namespace callie
