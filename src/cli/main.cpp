#include "callie/AccountManager.h"
#include "callie/AccountStore.h"
#include "callie/EventModel.h"
#include "callie/GoogleAuth.h"
#include "callie/GoogleCache.h"
#include "callie/GoogleCalendarApi.h"
#include "callie/GoogleSource.h"
#include "callie/GoogleSync.h"
#include "callie/GoogleTokenProvider.h"
#include "callie/LogFile.h"
#include "callie/SampleSource.h"
#include "callie/TokenStore.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <optional>

#include <unistd.h>

using namespace callie;

namespace {

QTextStream out(stdout);
QTextStream err(stderr);

bool useColor()
{
    static const bool tty = ::isatty(STDOUT_FILENO) && qEnvironmentVariableIsEmpty("NO_COLOR");
    return tty;
}

QString dim(const QString &s)
{
    return useColor() ? QStringLiteral("\033[2m%1\033[0m").arg(s) : s;
}
QString bold(const QString &s)
{
    return useColor() ? QStringLiteral("\033[1m%1\033[0m").arg(s) : s;
}
QString accent(const QString &s)
{
    return useColor() ? QStringLiteral("\033[36m%1\033[0m").arg(s) : s;
}

const QString kGoogle = QStringLiteral("google");

int runAgenda(int days, bool sample)
{
    const QTimeZone tz = QTimeZone::systemTimeZone();
    const QDate today = QDate::currentDate();
    const QDateTime from(today, QTime(0, 0), tz);
    const QDateTime to(today.addDays(days), QTime(0, 0), tz);

    QList<Event> events;
    if (sample) {
        events = SampleSource().eventsBetween(from, to, tz);
    } else {
        AccountStore store(AccountStore::defaultPath());
        QList<Account> accounts;
        if (!store.load(accounts)) {
            err << QStringLiteral("callie: %1\n").arg(store.errorString());
            return 1;
        }
        accounts.removeIf([](const Account &a) { return a.provider != kGoogle; });
        if (accounts.isEmpty()) {
            err << QObject::tr("No accounts. Add one with: callie accounts add google") << "\n";
            return 0;
        }
        GoogleCache cache(GoogleCache::defaultPath());
        if (!cache.open()) {
            err << QStringLiteral("callie: %1\n").arg(cache.errorString());
            return 1;
        }
        const GoogleSource source(cache, accounts);
        if (source.calendars().isEmpty()) {
            err << QObject::tr("Nothing synced yet. Run: callie sync") << "\n";
            return 0;
        }
        events = source.eventsBetween(from, to, tz);
    }
    std::sort(events.begin(), events.end(),
              [](const Event &a, const Event &b) { return a.start < b.start; });

    if (events.isEmpty()) {
        out << dim(QObject::tr("Nothing scheduled.")) << "\n";
        return 0;
    }

    QDate current;
    for (const Event &e : std::as_const(events)) {
        // Events already under way when the range starts are listed under today.
        const QDate day = std::max(e.start.date(), today);
        if (day != current) {
            current = day;
            const QString label = current == today
                                      ? QObject::tr("Today")
                                      : QLocale().toString(current, QStringLiteral("ddd d MMM"));
            out << "\n" << bold(label) << "\n";
        }

        const QString time =
            e.allDay ? QStringLiteral("all-day")
                     : QStringLiteral("%1–%2").arg(e.start.toString(QStringLiteral("HH:mm")),
                                                   e.end.toString(QStringLiteral("HH:mm")));

        out << QStringLiteral("  %1  %2").arg(dim(time.leftJustified(11)), e.summary);
        if (!e.conferenceUrl.isEmpty())
            out << "  " << accent(QStringLiteral("↗"));
        else if (!e.location.isEmpty())
            out << "  " << dim(e.location);
        out << "\n";
    }
    out << "\n";
    return 0;
}

/// Ends the event loop with `code` after flushing, since exit codes are the
/// only signal a script gets.
void finish(int code)
{
    out.flush();
    err.flush();
    QCoreApplication::exit(code);
}

int runAccountsList()
{
    AccountStore store(AccountStore::defaultPath());
    QList<Account> accounts;
    if (!store.load(accounts)) {
        err << QStringLiteral("callie: %1\n").arg(store.errorString());
        return 1;
    }
    if (accounts.isEmpty()) {
        err << QObject::tr("No accounts. Add one with: callie accounts add google") << "\n";
        return 0;
    }
    for (const Account &account : accounts)
        out << account.provider << "\t" << account.id << "\n";
    return 0;
}

/// The OAuth client, or nothing after telling the user how to configure one.
std::optional<GoogleClientConfig> googleClient()
{
    const GoogleClientConfig client = GoogleClientConfig::resolve();
    if (client.isValid())
        return client;
    err << QObject::tr("callie: no Google OAuth client is configured. Build with one, or set "
                       "CALLIE_GOOGLE_CLIENT_ID and CALLIE_GOOGLE_CLIENT_SECRET.")
        << "\n";
    return std::nullopt;
}

int runAccountsAddGoogle(QCoreApplication &app)
{
    const std::optional<GoogleClientConfig> client = googleClient();
    if (!client)
        return 1;

    GoogleAuth auth(*client);
    QNetworkAccessManager network;
    GoogleCalendarApi api(&network);
    KeychainTokenStore tokens;
    AccountStore store(AccountStore::defaultPath());
    AccountManager manager(tokens, store);

    QObject::connect(&auth, &GoogleAuth::authorizeUrlReady, [](const QUrl &url) {
        const QString link = url.toString(QUrl::FullyEncoded);
        err << QObject::tr("Opening your browser to sign in to Google. If it does not open, "
                           "visit:\n%1")
                   .arg(link)
            << "\n";
        err.flush();
        QProcess::startDetached(QStringLiteral("xdg-open"), {link});
    });
    QObject::connect(&manager, &AccountManager::connected, [](const Account &account) {
        out << account.id << "\n";
        finish(0);
    });
    QObject::connect(&manager, &AccountManager::failed, [](const QString &message) {
        err << QStringLiteral("callie: %1\n").arg(message);
        finish(1);
    });

    // Started from inside the loop: an exit() requested before exec() is ignored.
    QTimer::singleShot(0, &manager, [&] { manager.connectGoogle(auth, api); });
    return app.exec();
}

int runAccountsRemove(QCoreApplication &app, const Account &account)
{
    KeychainTokenStore tokens;
    AccountStore store(AccountStore::defaultPath());
    // A cache that cannot open must not keep an account from being removed.
    GoogleCache cache(GoogleCache::defaultPath());
    const bool cacheOpen = cache.open();
    if (!cacheOpen)
        err << QStringLiteral("callie: skipping cached events: %1\n").arg(cache.errorString());
    AccountManager manager(tokens, store, cacheOpen ? &cache : nullptr);

    QObject::connect(&manager, &AccountManager::removed, [] { finish(0); });
    QObject::connect(&manager, &AccountManager::failed, [](const QString &message) {
        err << QStringLiteral("callie: %1\n").arg(message);
        finish(1);
    });

    QTimer::singleShot(0, &manager, [&] { manager.remove(account); });
    return app.exec();
}

/// The log files, one path per line, newest first; or follows or opens them.
int runLogs(bool follow, bool open)
{
    const QString directory = logfile::defaultDirectory();
    if (open)
        return QProcess::execute(QStringLiteral("xdg-open"), {directory}) == 0 ? 0 : 1;

    QDir dir(directory);
    const QStringList names = dir.entryList({QStringLiteral("*.log")}, QDir::Files, QDir::Time);
    if (names.isEmpty()) {
        err << QObject::tr("No logs yet in %1").arg(directory) << "\n";
        return 0;
    }
    QStringList paths;
    for (const QString &name : names)
        paths.append(dir.filePath(name));
    if (follow) {
        out.flush();
        return QProcess::execute(QStringLiteral("tail"), QStringList{QStringLiteral("-F")} + paths);
    }
    for (const QString &path : std::as_const(paths))
        out << path << "\n";
    return 0;
}

/// Quiet on success. Each failure is one line on stderr, and any failure exits 1.
int runSync(QCoreApplication &app)
{
    AccountStore store(AccountStore::defaultPath());
    QList<Account> accounts;
    if (!store.load(accounts)) {
        err << QStringLiteral("callie: %1\n").arg(store.errorString());
        return 1;
    }
    accounts.removeIf([](const Account &a) { return a.provider != kGoogle; });
    if (accounts.isEmpty()) {
        err << QObject::tr("No accounts. Add one with: callie accounts add google") << "\n";
        return 0;
    }

    GoogleCache cache(GoogleCache::defaultPath());
    if (!cache.open()) {
        err << QStringLiteral("callie: %1\n").arg(cache.errorString());
        return 1;
    }
    const std::optional<GoogleClientConfig> client = googleClient();
    if (!client)
        return 1;
    KeychainTokenStore tokens;
    GoogleTokenProvider provider(*client, tokens);
    QNetworkAccessManager network;
    GoogleCalendarApi api(&network);
    GoogleSync sync(provider, api, cache);

    qsizetype remaining = accounts.size();
    bool failed = false;
    QTimer::singleShot(0, &app, [&] {
        for (const Account &account : std::as_const(accounts)) {
            sync.sync(account, [&, account](const QStringList &errors) {
                for (const QString &error : errors)
                    err << QStringLiteral("callie: %1: %2\n").arg(account.id, error);
                failed = failed || !errors.isEmpty();
                if (--remaining == 0)
                    finish(failed ? 1 : 0);
            });
        }
    });
    return app.exec();
}

/// One line per calendar, tab-separated: account, calendar id, name.
int runCalendars(QCoreApplication &app)
{
    AccountStore store(AccountStore::defaultPath());
    QList<Account> accounts;
    if (!store.load(accounts)) {
        err << QStringLiteral("callie: %1\n").arg(store.errorString());
        return 1;
    }
    accounts.removeIf([](const Account &a) { return a.provider != kGoogle; });
    if (accounts.isEmpty()) {
        err << QObject::tr("No accounts. Add one with: callie accounts add google") << "\n";
        return 0;
    }

    const std::optional<GoogleClientConfig> client = googleClient();
    if (!client)
        return 1;
    KeychainTokenStore tokens;
    GoogleTokenProvider provider(*client, tokens);
    QNetworkAccessManager network;
    GoogleCalendarApi api(&network);
    int failures = 0;

    std::function<void(qsizetype)> list = [&](qsizetype i) {
        if (i == accounts.size()) {
            finish(failures ? 1 : 0);
            return;
        }
        const Account account = accounts.at(i);
        const auto fail = [&, i, account](const QString &message) {
            err << QStringLiteral("callie: %1: %2\n").arg(account.id, message);
            ++failures;
            list(i + 1);
        };
        provider.accessToken(account, [&, i, account, fail](const QString &token,
                                                            const QString &error) {
            if (!error.isEmpty()) {
                fail(error);
                return;
            }
            api.fetchCalendars(token, [&, i, account, fail](const QList<GoogleCalendar> &calendars,
                                                            const GoogleApiError &apiError) {
                if (apiError) {
                    fail(apiError.message);
                    return;
                }
                for (const GoogleCalendar &calendar : calendars)
                    out << account.id << "\t" << calendar.id << "\t" << calendar.summary << "\n";
                list(i + 1);
            });
        });
    };

    QTimer::singleShot(0, &app, [&] { list(0); });
    return app.exec();
}

int runAccounts(QCoreApplication &app, const QStringList &args)
{
    const QString action = args.value(1, QStringLiteral("list"));
    const QString provider = args.value(2);

    if (action == QLatin1String("list") && args.size() <= 2)
        return runAccountsList();
    if (action == QLatin1String("add") && provider == kGoogle && args.size() == 3)
        return runAccountsAddGoogle(app);
    if (action == QLatin1String("remove") && provider == kGoogle && args.size() == 4)
        return runAccountsRemove(app, Account{kGoogle, args.at(3)});

    err << QObject::tr("usage: callie accounts [list | add google | remove google <id>]") << "\n";
    return 2;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("callie"));
    app.setApplicationVersion(QStringLiteral(CALLIE_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Callie: an elegant calendar for Linux.\n"
                       "\n"
                       "Commands:\n"
                       "  agenda     Upcoming events (default)\n"
                       "  accounts   List, add or remove calendar accounts\n"
                       "  calendars  List the calendars in each account\n"
                       "  logs       Show, follow (-f) or open (--open) the log files\n"
                       "  add        Create an event from natural language\n"
                       "  sync       Refresh all accounts now\n"
                       "  daemon     Run background sync and notifications\n"
                       "  gui        Launch the desktop app"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("Command to run."));

    QCommandLineOption daysOption({QStringLiteral("d"), QStringLiteral("days")},
                                  QStringLiteral("Days of agenda to show."), QStringLiteral("n"),
                                  QStringLiteral("7"));
    parser.addOption(daysOption);
    QCommandLineOption sampleOption(
        QStringLiteral("sample"), QStringLiteral("Show a made-up week instead of your calendars."));
    parser.addOption(sampleOption);
    QCommandLineOption followOption({QStringLiteral("f"), QStringLiteral("follow")},
                                    QStringLiteral("With logs: keep printing new lines."));
    parser.addOption(followOption);
    QCommandLineOption openOption(QStringLiteral("open"),
                                  QStringLiteral("With logs: open the folder."));
    parser.addOption(openOption);
    parser.process(app);
    logfile::install(QStringLiteral("callie"));

    const QStringList args = parser.positionalArguments();
    const QString command = args.isEmpty() ? QStringLiteral("agenda") : args.first();

    if (command == QLatin1String("agenda"))
        return runAgenda(parser.value(daysOption).toInt(), parser.isSet(sampleOption));

    if (command == QLatin1String("accounts"))
        return runAccounts(app, args);

    if (command == QLatin1String("logs") && args.size() == 1)
        return runLogs(parser.isSet(followOption), parser.isSet(openOption));

    if (command == QLatin1String("sync") && args.size() == 1)
        return runSync(app);

    if (command == QLatin1String("calendars") && args.size() == 1)
        return runCalendars(app);

    if (command == QLatin1String("gui"))
        return QProcess::execute(QStringLiteral("callie-gui"), {});

    if (command == QLatin1String("add") || command == QLatin1String("daemon")) {
        err << QStringLiteral("callie: '%1' is not implemented yet.\n").arg(command);
        return 2;
    }

    err << QStringLiteral("callie: unknown command '%1'\n").arg(command);
    parser.showHelp(1);
}
