#include "callie/LogFile.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QMutex>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>

namespace callie::logfile {

namespace {

constexpr int kKeptFiles = 3;

struct State
{
    QMutex mutex;
    QFile file;
    QtMessageHandler previous = nullptr;
    bool installed = false;
    qint64 maxBytes = 0;
    bool verboseTerminal = false;
    bool debug = false;
};

State &state()
{
    static State s;
    return s;
}

const char *levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return "debug";
    case QtInfoMsg: return "info";
    case QtWarningMsg: return "warning";
    case QtCriticalMsg: return "critical";
    case QtFatalMsg: return "fatal";
    }
    return "?";
}

// QtMsgType is not ordered by severity: QtInfoMsg comes after QtFatalMsg.
bool isProblem(QtMsgType type)
{
    return type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg;
}

void rotate(const QString &path, qint64 maxBytes)
{
    if (QFileInfo(path).size() <= maxBytes)
        return;
    QFile::remove(path + u'.' + QString::number(kKeptFiles));
    for (int i = kKeptFiles - 1; i >= 1; --i)
        QFile::rename(path + u'.' + QString::number(i), path + u'.' + QString::number(i + 1));
    QFile::rename(path, path + QStringLiteral(".1"));
}

void handle(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    State &s = state();
    // Debug output, QML's console.log included, can carry event details, so it
    // is kept off disk unless logging was turned up on purpose.
    if (type != QtDebugMsg || s.verboseTerminal || s.debug) {
        const QMutexLocker lock(&s.mutex);
        if (s.file.isOpen()) {
            QTextStream out(&s.file);
            out << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
                << ' ' << (context.category ? context.category : "default") << ' '
                << levelName(type) << ": " << message << '\n';
            out.flush();
            s.file.flush();
            // The app runs for days, so rotation cannot wait for the next start.
            if (s.file.size() > s.maxBytes) {
                s.file.close();
                rotate(s.file.fileName(), s.maxBytes);
                if (!s.file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
                    std::fprintf(stderr, "callie: could not reopen the log file\n");
            }
        }
    }
    // Info reaches the file by default, but the terminal only on request, so the
    // CLI stays quiet.
    if (isProblem(type) || s.verboseTerminal) {
        const QString line = qFormatLogMessage(type, context, message);
        std::fprintf(stderr, "%s\n", qPrintable(line));
        std::fflush(stderr);
    }
}

} // namespace

QString defaultDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) +
           QStringLiteral("/callie/logs");
}

bool install(const QString &name, const QString &directory, qint64 maxBytes)
{
    State &s = state();
    {
        const QMutexLocker lock(&s.mutex);
        s.maxBytes = maxBytes;
        s.verboseTerminal = !qEnvironmentVariableIsEmpty("QT_LOGGING_RULES");
        s.file.close();
    }
    // The handler goes in even without a file, or Qt's default one would print
    // info messages and the CLI would stop being quiet.
    if (!s.installed) {
        s.previous = qInstallMessageHandler(handle);
        s.installed = true;
    }

    const QMutexLocker lock(&s.mutex);
    if (!QDir().mkpath(directory))
        return false;
    const QString filePath = QDir(directory).filePath(name + QStringLiteral(".log"));
    rotate(filePath, maxBytes);
    s.file.setFileName(filePath);
    if (!s.file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return false;
    QTextStream(&s.file) << "--- " << name << ' ' << QCoreApplication::applicationVersion()
                         << " started, pid " << QCoreApplication::applicationPid() << " ---\n";
    s.file.flush();
    return true;
}

void setVerboseTerminal(bool verbose)
{
    State &s = state();
    const QMutexLocker lock(&s.mutex);
    s.verboseTerminal = verbose;
}

void setDebug(bool debug)
{
    {
        State &s = state();
        const QMutexLocker lock(&s.mutex);
        s.debug = debug;
    }
    QLoggingCategory::setFilterRules(debug ? QStringLiteral("callie.*.debug=true") : QString());
}

QString path()
{
    State &s = state();
    const QMutexLocker lock(&s.mutex);
    return s.file.isOpen() ? s.file.fileName() : QString();
}

void uninstall()
{
    State &s = state();
    if (s.installed)
        qInstallMessageHandler(s.previous);
    s.installed = false;
    const QMutexLocker lock(&s.mutex);
    s.file.close();
    s.previous = nullptr;
}

} // namespace callie::logfile
