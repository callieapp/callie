#include "FakeHttpServer.h"
#include "UpdateHint.h"
#include "Upgrade.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace callie::cli;
using namespace Qt::StringLiterals;

class TestUpgrade : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void updatesToTheLatest();
    void upToDateDownloadsNothing();
    void forceInstallsAgain();
    void downgradeGoesBackOneRelease();
    void mismatchedPackageIsNotInstalled();
    void dryRunChangesNothing();
    void refusesWhatItCannotDo();
    void hintsOnlyWhereWanted();
    void systemsWithoutPackagesSayWhy_data();
    void systemsWithoutPackagesSayWhy();
    void failuresStopBeforeInstalling();

private:
    /// A release with a package for each system, whose bodies name the version.
    QJsonObject release(const QString &version, bool prerelease = false) const;
    /// Runs the upgrade with Callie `installed` on `system`, and gives its exit code.
    int run(const Upgrade::Options &options, const QString &installed,
            const QString &system = u"fedora"_s);

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<FakeHttpServer> m_github;
    QHash<QString, QJsonObject> m_releases;
    QList<QStringList> m_ran;
    /// The exit code the package manager gives each command, by its program.
    QHash<QString, int> m_fails;
    bool m_devel = false;
    QString m_out;
    QString m_err;
    QStringList m_downloads;
    bool m_corrupt = false;
};

QJsonObject TestUpgrade::release(const QString &version, bool prerelease) const
{
    QJsonArray assets;
    for (const QString &name :
         {u"callie-%1-1.fc44.src.rpm"_s, u"callie-%1-1.fc44.x86_64.rpm"_s,
          u"callie_%1-1_amd64.deb"_s, u"callie-ubuntu_%1-1~ubuntu26.04_amd64.deb"_s,
          u"callie-dbgsym_%1-1_amd64.deb"_s}) {
        const QString file = name.arg(version);
        const QByteArray digest =
            QCryptographicHash::hash(file.toUtf8(), QCryptographicHash::Sha256).toHex();
        assets.append(QJsonObject{
            {u"name"_s, file},
            {u"digest"_s, u"sha256:"_s + QString::fromLatin1(digest)},
            {u"browser_download_url"_s, m_github->url(u"/download/"_s + file).toString()}});
    }
    return {{u"tag_name"_s, u"v"_s + version},
            {u"prerelease"_s, prerelease},
            {u"html_url"_s, u"https://github.com/greylocklabs/callie/releases/v"_s + version},
            {u"assets"_s, assets}};
}

void TestUpgrade::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    m_github = std::make_unique<FakeHttpServer>();
    m_ran.clear();
    m_downloads.clear();
    m_fails.clear();
    m_devel = false;
    m_corrupt = false;
    m_releases = {{u"0.2.1"_s, release(u"0.2.1"_s)},
                  {u"0.2.2"_s, release(u"0.2.2"_s)},
                  {u"0.2.3"_s, release(u"0.2.3"_s)},
                  {u"0.3.0"_s, release(u"0.3.0"_s, true)}};
    m_github->handler = [this](const FakeHttpServer::Request &request) -> FakeHttpServer::Response {
        const QString target = QString::fromUtf8(request.target);
        if (target == u"/releases/latest")
            return {200, QJsonDocument(m_releases.value(u"0.2.3"_s)).toJson()};
        if (target.startsWith(u"/releases/tags/v")) {
            const QString version = target.mid(16);
            return m_releases.contains(version)
                       ? FakeHttpServer::Response{200, QJsonDocument(m_releases[version]).toJson()}
                       : FakeHttpServer::Response{404, "{}"};
        }
        if (target.startsWith(u"/releases?")) {
            QJsonArray all;
            for (const QJsonObject &each : std::as_const(m_releases))
                all.append(each);
            return {200, QJsonDocument(all).toJson()};
        }
        if (target.startsWith(u"/download/")) {
            m_downloads.append(target.mid(10));
            return {200, m_corrupt ? QByteArray("not the package") : target.mid(10).toUtf8()};
        }
        return {404, "{}"};
    };
}

int TestUpgrade::run(const Upgrade::Options &options, const QString &installed,
                     const QString &system)
{
    QFile osRelease(m_dir->filePath(u"os-release"_s));
    if (!osRelease.open(QIODevice::WriteOnly))
        return -1;
    osRelease.write(
        system == u"fedora"   ? "ID=fedora\nVERSION_ID=44\nPRETTY_NAME=\"Fedora Linux 44\"\n"
        : system == u"ubuntu" ? "ID=ubuntu\nVERSION_ID=\"26.04\"\nPRETTY_NAME=\"Ubuntu 26.04\"\n"
                              : "ID=debian\nVERSION_ID=13\nPRETTY_NAME=\"Debian 13\"\n");
    osRelease.close();

    m_out.clear();
    m_err.clear();
    QTextStream out(&m_out);
    QTextStream err(&m_err);
    Upgrade upgrade(out, err);
    upgrade.setDevel(m_devel);
    upgrade.setApi(m_github->url(QString()));
    upgrade.setSystem(PackageSystem::detect(osRelease.fileName(), u"x86_64"_s));
    upgrade.setInstalled(installed);
    upgrade.setRunner([this](const QStringList &command) {
        // The package is there to install, and is the one that was checked.
        QFile package(command.last());
        if (command.last().startsWith(u'/') &&
            (!package.open(QIODevice::ReadOnly) ||
             !command.last().endsWith(QString::fromUtf8(package.readAll()))))
            return 1;
        m_ran.append(command);
        return m_fails.value(command.at(command.first() == u"sudo" ? 1 : 0) + u' ' +
                             command.at(command.first() == u"sudo" ? 2 : 1));
    });
    std::optional<int> result;
    upgrade.run(options, [&result](int code) { result = code; });
    if (!QTest::qWaitFor([&result] { return result.has_value(); }))
        return -1;
    return *result;
}

/// The commands as run, without sudo or the downloaded file's folder.
static QList<QStringList> plain(QList<QStringList> commands)
{
    for (QStringList &command : commands) {
        command.removeAll(u"sudo"_s);
        if (command.last().startsWith(u'/'))
            command.last() = command.last().section(u'/', -1);
    }
    return commands;
}

void TestUpgrade::updatesToTheLatest()
{
    QCOMPARE(run({}, u"0.2.1"_s), 0);
    QVERIFY(m_out.contains(u"Updating Callie 0.2.1 to 0.2.3."_s));
    QCOMPARE(m_downloads, QStringList{u"callie-0.2.3-1.fc44.x86_64.rpm"_s});
    QCOMPARE(plain(m_ran), (QList<QStringList>{{u"dnf"_s, u"install"_s, u"-y"_s,
                                                u"callie-0.2.3-1.fc44.x86_64.rpm"_s}}));
    // A package's revision is not part of its release.
    QCOMPARE(run({}, u"0.2.3-1"_s), 0);
    QVERIFY(m_out.contains(u"up to date"_s));

    // Debian and Ubuntu each get the deb built for them.
    QCOMPARE(run({}, u"0.2.1"_s, u"debian"_s), 0);
    QCOMPARE(m_downloads.last(), u"callie_0.2.3-1_amd64.deb"_s);
    QCOMPARE(run({}, u"0.2.1"_s, u"ubuntu"_s), 0);
    QCOMPARE(m_downloads.last(), u"callie-ubuntu_0.2.3-1~ubuntu26.04_amd64.deb"_s);
    // Ubuntu's own revision is not part of the release either.
    QCOMPARE(run({}, u"0.2.3-1~ubuntu26.04"_s, u"ubuntu"_s), 0);
    QVERIFY(m_out.contains(u"up to date"_s));
}

void TestUpgrade::upToDateDownloadsNothing()
{
    QCOMPARE(run({}, u"0.2.3"_s, u"debian"_s), 0);
    QCOMPARE(m_out, u"Callie 0.2.3 is installed and up to date.\n"_s);
    QVERIFY(m_downloads.isEmpty());
    QVERIFY(m_ran.isEmpty());
    // A source build newer than any release is left alone.
    QCOMPARE(run({}, u"0.2.9"_s), 0);
    QVERIFY(m_out.contains(u"newer than the latest release, 0.2.3"_s));
    QVERIFY(m_ran.isEmpty());
}

void TestUpgrade::forceInstallsAgain()
{
    Upgrade::Options options;
    options.force = true;
    QCOMPARE(run(options, u"0.2.3"_s, u"debian"_s), 0);
    QCOMPARE(plain(m_ran), (QList<QStringList>{{u"apt-get"_s, u"update"_s},
                                               {u"apt-get"_s, u"install"_s, u"-y"_s,
                                                u"--reinstall"_s, u"callie_0.2.3-1_amd64.deb"_s}}));
}

void TestUpgrade::downgradeGoesBackOneRelease()
{
    // The 0.3.0 pre-release is passed over.
    Upgrade::Options options;
    options.downgrade = true;
    QCOMPARE(run(options, u"0.2.3"_s, u"debian"_s), 0);
    QVERIFY(m_out.contains(u"Going back from Callie 0.2.3 to 0.2.2."_s));
    QCOMPARE(plain(m_ran).last(),
             (QStringList{u"apt-get"_s, u"install"_s, u"-y"_s, u"--allow-downgrades"_s,
                          u"callie_0.2.2-1_amd64.deb"_s}));
    m_ran.clear();
    options.version = u"0.2.1"_s;
    QCOMPARE(run(options, u"0.2.3"_s), 0);
    QCOMPARE(plain(m_ran), (QList<QStringList>{{u"dnf"_s, u"downgrade"_s, u"-y"_s,
                                                u"callie-0.2.1-1.fc44.x86_64.rpm"_s}}));
    options.version.reset();
    QCOMPARE(run(options, u"0.2.1"_s), 1);
    QVERIFY(m_err.contains(u"no release older than 0.2.1"_s));
}

void TestUpgrade::mismatchedPackageIsNotInstalled()
{
    m_corrupt = true;
    QCOMPARE(run({}, u"0.2.1"_s), 1);
    QVERIFY(m_err.contains(u"does not match its checksum"_s));
    QVERIFY(m_ran.isEmpty());
}

void TestUpgrade::dryRunChangesNothing()
{
    Upgrade::Options options;
    options.dryRun = true;
    QCOMPARE(run(options, u"0.2.1"_s), 0);
    QVERIFY(m_out.contains(u"/download/callie-0.2.3-1.fc44.x86_64.rpm"_s));
    QVERIFY(m_out.contains(u"dnf install -y"_s));
    QVERIFY(m_downloads.isEmpty());
    QVERIFY(m_ran.isEmpty());
}

void TestUpgrade::refusesWhatItCannotDo()
{
    // Each way round, by the command meant for it.
    Upgrade::Options older;
    older.version = u"0.2.1"_s;
    QCOMPARE(run(older, u"0.2.3"_s), 2);
    QVERIFY(m_err.contains(u"callie downgrade 0.2.1"_s));
    Upgrade::Options newer;
    newer.downgrade = true;
    newer.version = u"0.2.3"_s;
    QCOMPARE(run(newer, u"0.2.1"_s), 2);
    QVERIFY(m_err.contains(u"callie upgrade 0.2.3"_s));

    Upgrade::Options missing;
    missing.version = u"9.9.9"_s;
    QCOMPARE(run(missing, u"0.2.1"_s), 1);
    QVERIFY(m_err.contains(u"GitHub has no v9.9.9"_s));

    // Not from a package: the installer is the way in.
    QCOMPARE(run({}, QString()), 1);
    QVERIFY(m_err.contains(u"install.sh"_s));
    QVERIFY(m_ran.isEmpty());
}

void TestUpgrade::hintsOnlyWhereWanted()
{
    // 0.2.3 is known to be out, and GitHub was asked just now.
    UpdateCheck check(m_dir->filePath(u"latest-release.json"_s));
    check.setDevel(false);
    {
        QFile file(m_dir->filePath(u"latest-release.json"_s));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{u"version"_s, u"0.2.3"_s},
                                             {u"asked"_s, QDateTime::currentDateTimeUtc().toString(
                                                              Qt::ISODateWithMs)}})
                       .toJson());
    }
    const QVersionNumber running(0, 2, 1);
    const auto hint = [&](const QString &command, bool json, bool terminal, bool setting) {
        return UpdateHint::after(command, json, terminal, setting, check, running);
    };
    UpdateHint said = hint(u"agenda"_s, false, true, true);
    QCOMPARE(said.line,
             u"callie: Callie 0.2.3 is out, and this is 0.2.1. Update with: callie upgrade"_s);
    QVERIFY(!said.ask);
    // Not for scripts, not off a terminal, not about releases, and not once turned off.
    QVERIFY(hint(u"agenda"_s, true, true, true).line.isEmpty());
    QVERIFY(hint(u"agenda"_s, false, false, true).line.isEmpty());
    QVERIFY(hint(u"upgrade"_s, false, true, true).line.isEmpty());
    QVERIFY(hint(u"__complete"_s, false, true, true).line.isEmpty());
    said = hint(u"settings"_s, false, true, false);
    QVERIFY(said.line.isEmpty());
    QVERIFY(!said.ask);
    // Nor in a development build.
    check.setDevel(true);
    QVERIFY(hint(u"agenda"_s, false, true, true).line.isEmpty());
    check.setDevel(false);
    // Nor in a Flatpak.
    qputenv("CALLIE_FLATPAK", "1");
    QVERIFY(!check.wanted(true));
    QVERIFY(hint(u"agenda"_s, false, true, true).line.isEmpty());
    qputenv("CALLIE_FLATPAK", "0");
    // Up to date, it says nothing; a day on, it asks again.
    QVERIFY(UpdateHint::after(u"agenda"_s, false, true, true, check, QVersionNumber(0, 2, 3))
                .line.isEmpty());
    check.setNow([] { return QDateTime::currentDateTimeUtc().addDays(2); });
    QVERIFY(hint(u"agenda"_s, false, true, true).ask);
}

void TestUpgrade::systemsWithoutPackagesSayWhy_data()
{
    QTest::addColumn<QByteArray>("osRelease");
    QTest::addColumn<QString>("architecture");
    QTest::addColumn<int>("kind");
    QTest::addColumn<QString>("why");

    const auto rpm = int(PackageSystem::Kind::Rpm);
    const auto deb = int(PackageSystem::Kind::Deb);
    const auto none = int(PackageSystem::Kind::Unsupported);
    QTest::newRow("fedora 44") << QByteArray("ID=fedora\nVERSION_ID=44\n") << u"x86_64"_s << rpm
                               << QString();
    QTest::newRow("fedora 45") << QByteArray("ID=fedora\nVERSION_ID=45\n") << u"x86_64"_s << rpm
                               << QString();
    QTest::newRow("fedora 43") << QByteArray("ID=fedora\nVERSION_ID=43\nPRETTY_NAME='Fedora 43'\n")
                               << u"x86_64"_s << none << u"Fedora 43 is older than Fedora 44"_s;
    QTest::newRow("fedora rawhide") << QByteArray("ID=fedora\nVERSION_ID=rawhide\n") << u"x86_64"_s
                                    << none << u"cannot read the Fedora version"_s;
    QTest::newRow("debian 13") << QByteArray("ID=debian\nVERSION_ID=\"13\"\n") << u"x86_64"_s << deb
                               << QString();
    QTest::newRow("debian testing") << QByteArray("ID=debian\nPRETTY_NAME=\"Debian forky/sid\"\n")
                                    << u"x86_64"_s << deb << QString();
    QTest::newRow("debian 12") << QByteArray("ID=debian\nVERSION_ID=12\nPRETTY_NAME=Debian 12\n")
                               << u"x86_64"_s << none << u"older than Debian 13"_s;
    QTest::newRow("ubuntu 26.04") << QByteArray("ID=ubuntu\nVERSION_ID=\"26.04\"\n") << u"x86_64"_s
                                  << deb << QString();
    QTest::newRow("ubuntu 24.04") << QByteArray("ID=ubuntu\nVERSION_ID=24.04\n") << u"x86_64"_s
                                  << none << u"older than Ubuntu 26.04"_s;
    QTest::newRow("arch") << QByteArray("ID=arch\nPRETTY_NAME=\"Arch Linux\"\n") << u"x86_64"_s
                          << none << u"Arch Linux is not one Callie ships packages for"_s;
    QTest::newRow("arm") << QByteArray("ID=fedora\nVERSION_ID=44\n") << u"arm64"_s << none
                         << u"no packages for arm64"_s;
    QTest::newRow("unknown") << QByteArray() << u"x86_64"_s << none << u"is missing"_s;
}

void TestUpgrade::systemsWithoutPackagesSayWhy()
{
    QFETCH(QByteArray, osRelease);
    QFETCH(QString, architecture);
    QFETCH(int, kind);
    QFETCH(QString, why);
    QString path = m_dir->filePath(u"missing"_s);
    if (!osRelease.isEmpty()) {
        QFile file(m_dir->filePath(u"os-release"_s));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(osRelease);
        path = file.fileName();
    }
    const PackageSystem system = PackageSystem::detect(path, architecture);
    QCOMPARE(int(system.kind), kind);
    QVERIFY2(why.isEmpty() ? system.why.isEmpty() : system.why.contains(why),
             qPrintable(system.why));
}

void TestUpgrade::failuresStopBeforeInstalling()
{
    // apt cannot refresh its lists: the package is not installed after all.
    m_fails.insert(u"apt-get update"_s, 100);
    QCOMPARE(run({}, u"0.2.1"_s, u"debian"_s), 1);
    QVERIFY(m_err.contains(u"this did not work"_s));
    QCOMPARE(m_ran.size(), 1);
    m_fails.clear();
    m_ran.clear();
    m_downloads.clear();

    // A release without this system's package, or without its checksum.
    QJsonObject bare = m_releases[u"0.2.3"_s];
    QJsonArray assets = bare[u"assets"_s].toArray();
    QJsonObject rpm = assets.at(1).toObject();
    rpm.remove(u"digest"_s);
    assets[1] = rpm;
    assets.removeAt(2);
    bare[u"assets"_s] = assets;
    m_releases[u"0.2.3"_s] = bare;
    QCOMPARE(run({}, u"0.2.1"_s, u"debian"_s), 1);
    QVERIFY(m_err.contains(u"has no package for Debian 13"_s));
    QCOMPARE(run({}, u"0.2.1"_s), 1);
    QVERIFY(m_err.contains(u"no checksum for callie-0.2.3-1.fc44.x86_64.rpm"_s));
    QVERIFY(m_downloads.isEmpty());

    // A development build updates from its source.
    m_devel = true;
    QCOMPARE(run({}, u"0.2.1"_s), 1);
    QVERIFY(m_err.contains(u"git pull"_s));
    QVERIFY(m_ran.isEmpty());
    m_devel = false;

    // A Flatpak is Flathub's to update.
    qputenv("CALLIE_FLATPAK", "1");
    QCOMPARE(run({}, u"0.2.1"_s), 1);
    qputenv("CALLIE_FLATPAK", "0");
    QVERIFY(m_err.contains(u"flatpak update org.callieapp.Callie"_s));
    QVERIFY(m_ran.isEmpty());
    QVERIFY(m_downloads.isEmpty());
}

QTEST_MAIN(TestUpgrade)
#include "tst_upgrade.moc"
