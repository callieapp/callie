#include "callie/LogFile.h"

#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>
#include <functional>

#include <fcntl.h>
#include <unistd.h>

using namespace callie;
using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcTest, "callie.test", QtInfoMsg)

namespace {

QString read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

/// Runs `log` with stderr pointed at a file, and returns what reached it.
QString terminalOutput(const QString &capture, const std::function<void()> &log)
{
    std::fflush(stderr);
    const int saved = ::dup(STDERR_FILENO);
    const int file = ::open(QFile::encodeName(capture).constData(), O_WRONLY | O_CREAT, 0600);
    ::dup2(file, STDERR_FILENO);
    log();
    std::fflush(stderr);
    ::dup2(saved, STDERR_FILENO);
    ::close(file);
    ::close(saved);
    return read(capture);
}

} // namespace

class TestLogFile : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void cleanup() { logfile::uninstall(); }

    void infoReachesTheFile();
    void debugStaysOutByDefault();
    void eachRunStartsWithAHeader();
    void largeFileIsRotated();
    void rotationKeepsThreeOldFiles();
    void unwritableDirectoryFails();
    void rotatesWhileRunning();
    void plainDebugStaysOffDisk();
    void terminalShowsOnlyProblems();
    void verboseShowsProgressAndKeepsDebug();
};

void TestLogFile::infoReachesTheFile()
{
    QTemporaryDir dir;
    QVERIFY(logfile::install(u"app"_s, dir.path()));
    QCOMPARE(logfile::path(), dir.filePath(u"app.log"_s));

    qCInfo(lcTest) << "synced 3 calendars";

    const QString log = read(dir.filePath(u"app.log"_s));
    QVERIFY2(log.contains(u"callie.test info: synced 3 calendars"_s), qPrintable(log));
}

void TestLogFile::debugStaysOutByDefault()
{
    QTemporaryDir dir;
    QVERIFY(logfile::install(u"app"_s, dir.path()));

    qCDebug(lcTest) << "noisy detail";

    QVERIFY(!read(dir.filePath(u"app.log"_s)).contains(u"noisy detail"_s));
}

void TestLogFile::eachRunStartsWithAHeader()
{
    QTemporaryDir dir;
    QVERIFY(logfile::install(u"app"_s, dir.path()));
    logfile::uninstall();
    QVERIFY(logfile::install(u"app"_s, dir.path()));

    QCOMPARE(read(dir.filePath(u"app.log"_s)).count(u"--- app "_s), 2);
}

void TestLogFile::largeFileIsRotated()
{
    QTemporaryDir dir;
    QFile old(dir.filePath(u"app.log"_s));
    QVERIFY(old.open(QIODevice::WriteOnly));
    old.write(QByteArray(200, 'x'));
    old.close();

    QVERIFY(logfile::install(u"app"_s, dir.path(), 100));

    QCOMPARE(read(dir.filePath(u"app.log.1"_s)), QString(200, u'x'));
    QVERIFY(!read(dir.filePath(u"app.log"_s)).contains(u'x'));
}

void TestLogFile::rotationKeepsThreeOldFiles()
{
    QTemporaryDir dir;
    for (int run = 0; run < 6; ++run) {
        QFile file(dir.filePath(u"app.log"_s));
        QVERIFY(file.open(QIODevice::Append));
        file.write(QByteArray(200, 'x'));
        file.close();
        QVERIFY(logfile::install(u"app"_s, dir.path(), 100));
        logfile::uninstall();
    }

    for (const char *suffix : {".1", ".2", ".3"})
        QVERIFY(QFile::exists(dir.filePath(u"app.log"_s + QLatin1String(suffix))));
    QVERIFY(!QFile::exists(dir.filePath(u"app.log.4"_s)));
}

void TestLogFile::unwritableDirectoryFails()
{
    QTemporaryDir dir;
    QFile blocker(dir.filePath(u"not-a-dir"_s));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();

    const QtMessageHandler before = qInstallMessageHandler(nullptr);
    qInstallMessageHandler(before);

    QVERIFY(!logfile::install(u"app"_s, blocker.fileName() + u"/logs"_s));
    QCOMPARE(logfile::path(), QString());

    // Without a file the handler still filters the terminal, or the CLI would
    // print every info message.
    const QtMessageHandler current = qInstallMessageHandler(nullptr);
    qInstallMessageHandler(current);
    QVERIFY(current != before);
}

void TestLogFile::rotatesWhileRunning()
{
    QTemporaryDir dir;
    QVERIFY(logfile::install(u"app"_s, dir.path(), 200));

    for (int i = 0; i < 10; ++i)
        qCInfo(lcTest) << "line" << i << "of a long-running app";

    QVERIFY(QFile::exists(dir.filePath(u"app.log.1"_s)));
    QVERIFY(QFileInfo(dir.filePath(u"app.log"_s)).size() <= 200);
    QVERIFY(read(dir.filePath(u"app.log"_s)).contains(u"line 9"_s));
}

void TestLogFile::plainDebugStaysOffDisk()
{
    // QML's console.log arrives as a debug message like this one.
    // Some distributions turn debug output off everywhere, which would make
    // this pass without reaching the log file's own filter.
    QLoggingCategory::setFilterRules(u"default.debug=true"_s);
    QTemporaryDir dir;
    QVERIFY(logfile::install(u"app"_s, dir.path()));

    qDebug() << "event title from console.log";
    QLoggingCategory::setFilterRules({});

    QVERIFY(!read(dir.filePath(u"app.log"_s)).contains(u"event title"_s));
}

void TestLogFile::terminalShowsOnlyProblems()
{
    QTemporaryDir dir;
    QVERIFY(logfile::install(u"app"_s, dir.path()));

    const QString terminal = terminalOutput(dir.filePath(u"stderr.txt"_s), [] {
        qCInfo(lcTest) << "quiet progress";
        qCWarning(lcTest) << "loud problem";
    });
    QVERIFY2(terminal.contains(u"loud problem"_s), qPrintable(terminal));
    QVERIFY2(!terminal.contains(u"quiet progress"_s), qPrintable(terminal));
}

void TestLogFile::verboseShowsProgressAndKeepsDebug()
{
    QTemporaryDir dir;
    QVERIFY(logfile::install(u"app"_s, dir.path()));
    logfile::setVerboseTerminal(true);
    QLoggingCategory::setFilterRules(u"callie.test.debug=true"_s);

    const QString terminal = terminalOutput(dir.filePath(u"stderr.txt"_s), [] {
        qCInfo(lcTest) << "quiet progress";
        qCDebug(lcTest) << "fine detail";
    });
    QLoggingCategory::setFilterRules({});

    QVERIFY2(terminal.contains(u"quiet progress"_s), qPrintable(terminal));
    // Asked-for debug output is kept, since whoever turned it on wants it.
    QVERIFY(read(dir.filePath(u"app.log"_s)).contains(u"fine detail"_s));
}

QTEST_GUILESS_MAIN(TestLogFile)
#include "tst_logfile.moc"
