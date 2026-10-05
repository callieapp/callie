#include "callie/LogFile.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
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
    bool verboseTerminal = false;
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
    {
        const QMutexLocker lock(&s.mutex);
        if (s.file.isOpen()) {
            QTextStream out(&s.file);
            out << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
                << ' ' << (context.category ? context.category : "default") << ' '
                << levelName(type) << ": " << message << '\n';
            out.flush();
            s.file.flush();
        }
    }
    // Info and debug reach the file by default, but the terminal only on request,
    // so the CLI stays quiet.
    if (type >= QtWarningMsg || s.verboseTerminal) {
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
    const QMutexLocker lock(&s.mutex);
    if (!QDir().mkpath(directory))
        return false;
    const QString filePath = QDir(directory).filePath(name + QStringLiteral(".log"));
    rotate(filePath, maxBytes);
    s.file.close();
    s.file.setFileName(filePath);
    if (!s.file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return false;
    s.verboseTerminal = !qEnvironmentVariableIsEmpty("QT_LOGGING_RULES");

    QTextStream(&s.file) << "--- " << name << ' ' << QCoreApplication::applicationVersion()
                         << " started, pid " << QCoreApplication::applicationPid() << " ---\n";
    s.file.flush();
    s.previous = qInstallMessageHandler(handle);
    return true;
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
    qInstallMessageHandler(s.previous);
    const QMutexLocker lock(&s.mutex);
    s.file.close();
    s.previous = nullptr;
}

} // namespace callie::logfile
