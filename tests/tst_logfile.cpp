#include "callie/LogFile.h"

#include <QFile>
#include <QLoggingCategory>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcTest, "callie.test", QtInfoMsg)

namespace {

QString read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
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

    QVERIFY(!logfile::install(u"app"_s, blocker.fileName() + u"/logs"_s));
    QCOMPARE(logfile::path(), QString());
}

QTEST_GUILESS_MAIN(TestLogFile)
#include "tst_logfile.moc"
