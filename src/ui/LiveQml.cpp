#include "LiveQml.h"

#include "callie/Logging.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>

namespace callie {

LiveQml::LiveQml(QQmlApplicationEngine &engine, QList<Module> modules, std::function<void()> load,
                 QObject *parent)
    : QObject(parent), m_engine(engine), m_modules(std::move(modules)), m_load(std::move(load))
{
    // Editors often save in several steps; reload once they settle.
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(100);
    connect(&m_debounce, &QTimer::timeout, this, &LiveQml::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce,
            qOverload<>(&QTimer::start));
}

bool LiveQml::start()
{
    if (!m_dir.isValid()) {
        m_error = tr("could not create a temporary folder");
        return false;
    }
    for (const Module &module : std::as_const(m_modules)) {
        if (!mirror(module))
            return false;
    }
    m_engine.addImportPath(m_dir.path());
    watchSources();
    for (const Module &module : std::as_const(m_modules))
        qCInfo(lcUi) << "live QML: serving" << module.uri << "from" << module.sourceDir;
    return true;
}

bool LiveQml::mirror(const Module &module)
{
    const QDir target(m_dir.filePath(QString(module.uri).replace(u'.', u'/')));
    if (!QDir().mkpath(target.path())) {
        m_error = tr("could not create %1").arg(target.path());
        return false;
    }

    // The generated qmldir, minus `prefer`, which would send Qt back to the
    // copies compiled into the binary.
    QFile in(QDir(module.buildDir).filePath(QStringLiteral("qmldir")));
    QFile out(target.filePath(QStringLiteral("qmldir")));
    if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly)) {
        m_error = tr("could not copy %1").arg(in.fileName());
        return false;
    }
    while (!in.atEnd()) {
        const QByteArray line = in.readLine();
        if (!line.startsWith("prefer "))
            out.write(line);
    }

    const QDir source(module.sourceDir);
    for (const QString &name : source.entryList({QStringLiteral("*.qml")}, QDir::Files)) {
        const QString link = target.filePath(name);
        if (!QFile::exists(link))
            QFile::link(source.filePath(name), link);
    }
    return true;
}

void LiveQml::watchSources()
{
    // Saving by rename drops a file from the watch list, so it is rebuilt each time.
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    for (const Module &module : std::as_const(m_modules)) {
        const QDir source(module.sourceDir);
        if (!m_watcher.directories().contains(source.path()))
            m_watcher.addPath(source.path());
        for (const QString &name : source.entryList({QStringLiteral("*.qml")}, QDir::Files))
            m_watcher.addPath(source.filePath(name));
    }
}

void LiveQml::reload()
{
    // New files need links before the reload can see them.
    for (const Module &module : std::as_const(m_modules))
        mirror(module);
    watchSources();

    // Replacing the only window must not count as closing it.
    const bool quitOnClose = QGuiApplication::quitOnLastWindowClosed();
    QGuiApplication::setQuitOnLastWindowClosed(false);

    QRect geometry;
    const QList<QObject *> roots = m_engine.rootObjects();
    for (QObject *root : roots) {
        if (auto *window = qobject_cast<QQuickWindow *>(root); window && geometry.isNull())
            geometry = window->geometry();
        root->deleteLater();
    }

    // Deleting a window takes an event loop turn, and the cache must forget the
    // old components before the new load.
    QTimer::singleShot(0, this, [this, geometry, quitOnClose] {
        m_engine.clearComponentCache();
        m_load();
        const QList<QObject *> roots = m_engine.rootObjects();
        if (auto *window = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(roots.last());
            window && !geometry.isNull())
            window->setGeometry(geometry);
        QGuiApplication::setQuitOnLastWindowClosed(quitOnClose);
        // The engine has already logged why a load failed.
        if (!roots.isEmpty())
            qCInfo(lcUi) << "live QML: reloaded";
    });
}

} // namespace callie
