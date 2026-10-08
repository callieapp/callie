#include "callie/AccountManager.h"
#include "callie/AccountStore.h"
#include "callie/Diagnostics.h"
#include "callie/EventModel.h"
#include "callie/GoogleAuth.h"
#include "callie/GoogleCache.h"
#include "callie/GoogleCalendarApi.h"
#include "callie/GoogleSource.h"
#include "callie/GoogleSync.h"
#include "callie/GoogleTokenProvider.h"
#include "callie/LogFile.h"
#include "callie/SampleSource.h"
#include "callie/Settings.h"
#include "callie/TokenStore.h"

#include "Commands.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <optional>
#include <tuple>

#include <unistd.h>

using namespace callie;
using namespace Qt::StringLiterals;

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
QString warn(const QString &s)
{
    return useColor() ? QStringLiteral("\033[31m%1\033[0m").arg(s) : s;
}

const QString kGoogle = QStringLiteral("google");

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

/// How long ago `when` was, roughly, or "never".
QString ago(const QDateTime &when)
{
    if (!when.isValid())
        return QObject::tr("never");
    const qint64 minutes = when.secsTo(QDateTime::currentDateTimeUtc()) / 60;
    if (minutes < 1)
        return QObject::tr("just now");
    if (minutes < 60)
        return QObject::tr("%1 min ago").arg(minutes);
    if (minutes < 48 * 60)
        return QObject::tr("%1 h ago").arg(minutes / 60);
    return QObject::tr("%1 days ago").arg(minutes / (24 * 60));
}

QString describe(const SyncState &state)
{
    QString text = QObject::tr("synced %1").arg(ago(state.lastSynced));
    if (!state.lastError.isEmpty())
        text += u"  "_s + warn(QObject::tr("failed: %1").arg(state.lastError));
    return text;
}

/// Everything worth checking before reporting a sync problem.
int runStatus(QCoreApplication &app)
{
    const bool client = GoogleClientConfig::resolve().isValid();
    out << bold(QObject::tr("Google OAuth client").leftJustified(21))
        << (client ? QObject::tr("configured") : warn(QObject::tr("not configured"))) << "\n";
    out << bold(QObject::tr("Logs").leftJustified(21)) << logfile::defaultDirectory() << "\n";
    out << bold(QObject::tr("Cache").leftJustified(21)) << GoogleCache::defaultPath() << "\n";

    AccountStore store(AccountStore::defaultPath());
    QList<Account> accounts;
    if (!store.load(accounts)) {
        err << QStringLiteral("callie: %1\n").arg(store.errorString());
        return 1;
    }
    accounts.removeIf([](const Account &a) { return a.provider != kGoogle; });
    if (accounts.isEmpty()) {
        out << "\n" << QObject::tr("No accounts. Add one with: callie accounts add google") << "\n";
        return 0;
    }
    GoogleCache cache(GoogleCache::defaultPath());
    if (!cache.open()) {
        err << QStringLiteral("callie: %1\n").arg(cache.errorString());
        return 1;
    }

    // The keyring answers asynchronously, so accounts print once each read is back.
    KeychainTokenStore tokens;
    qsizetype next = 0;
    std::function<void()> printNext = [&] {
        if (next == accounts.size()) {
            finish(0);
            return;
        }
        const Account account = accounts.at(next++);
        tokens.read(account, [&, account](const QString &secret, const QString &error) {
            const QString token = !error.isEmpty()
                                      ? warn(QObject::tr("keyring error: %1").arg(error))
                                  : secret.isEmpty() ? warn(QObject::tr("no token stored"))
                                                     : QObject::tr("token stored");
            out << "\n"
                << bold(account.id) << "  " << token << "  "
                << describe(cache.accountState(account)) << "\n";
            for (const GoogleCalendar &calendar : cache.calendars(account)) {
                out << "  " << calendar.summary.leftJustified(28, u' ', true) << "  "
                    << describe(cache.calendarState(account, calendar.id));
                if (!calendar.selected)
                    out << "  " << dim(QObject::tr("hidden"));
                out << "\n";
            }
            printNext();
        });
    };
    QTimer::singleShot(0, &app, [&] { printNext(); });
    return app.exec();
}

/// The bug report details, with email addresses masked. `report` also opens a
/// new GitHub issue with them filled in.
int runDoctor(QCoreApplication &app, bool report)
{
    KeychainTokenStore tokens;
    QTimer::singleShot(0, &app, [&] {
        diagnostics::collect(tokens, {}, [report](const QString &text) {
            out << text << "\n";
            if (report) {
                const QString url = diagnostics::issueUrl(text).toString(QUrl::FullyEncoded);
                // Over SSH or without a desktop nothing opens, so the link is always shown.
                if (QProcess::startDetached(QStringLiteral("xdg-open"), {url}))
                    err << QObject::tr("Opening a new issue in your browser. If it does not "
                                       "open, visit:")
                        << "\n";
                else
                    err << QObject::tr("Could not open a browser. To report the bug, visit:")
                        << "\n";
                err << url << "\n";
            }
            finish(0);
        });
    });
    return app.exec();
}

/// The log files, one path per line, newest first; or follows or opens them.
int runLogs(bool follow, bool open)
{
    const QString directory = logfile::defaultDirectory();
    if (open)
        return QProcess::execute(QStringLiteral("xdg-open"), {directory}) == 0 ? 0 : 1;

    QDir dir(directory);
    const QStringList names = dir.entryList(
        {QStringLiteral("*.log"), QStringLiteral("*.log.[0-9]")}, QDir::Files, QDir::Time);
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

/// Where the event commands read and write: sample data, or the Google
/// accounts through the cache, with the network for changes.
struct Backend
{
    QList<Account> accounts;
    std::unique_ptr<GoogleCache> cache;
    std::unique_ptr<KeychainTokenStore> tokens;
    std::unique_ptr<GoogleTokenProvider> provider;
    std::unique_ptr<QNetworkAccessManager> network;
    std::unique_ptr<GoogleCalendarApi> api;
    std::unique_ptr<GoogleSync> sync;
    std::unique_ptr<CalendarSource> source;
};

/// Opens the backend, or says why it cannot and returns null with `code` set.
std::unique_ptr<Backend> openBackend(bool sample, bool writing, int &code)
{
    auto backend = std::make_unique<Backend>();
    // Nothing to read is no error, but nothing to change is.
    code = writing ? 1 : 0;
    if (sample) {
        backend->source = std::make_unique<SampleSource>();
        return backend;
    }
    AccountStore store(AccountStore::defaultPath());
    if (!store.load(backend->accounts)) {
        err << QStringLiteral("callie: %1\n").arg(store.errorString());
        code = 1;
        return nullptr;
    }
    backend->accounts.removeIf([](const Account &a) { return a.provider != kGoogle; });
    if (backend->accounts.isEmpty()) {
        err << QObject::tr("No accounts. Add one with: callie accounts add google") << "\n";
        return nullptr;
    }
    backend->cache = std::make_unique<GoogleCache>(GoogleCache::defaultPath());
    if (!backend->cache->open()) {
        err << QStringLiteral("callie: %1\n").arg(backend->cache->errorString());
        code = 1;
        return nullptr;
    }
    auto google = std::make_unique<GoogleSource>(*backend->cache, backend->accounts);
    if (google->calendars().isEmpty()) {
        err << QObject::tr("Nothing synced yet. Run: callie sync") << "\n";
        return nullptr;
    }
    if (writing) {
        const std::optional<GoogleClientConfig> client = googleClient();
        if (!client) {
            code = 1;
            return nullptr;
        }
        backend->tokens = std::make_unique<KeychainTokenStore>();
        backend->provider = std::make_unique<GoogleTokenProvider>(*client, *backend->tokens);
        backend->network = std::make_unique<QNetworkAccessManager>();
        backend->api = std::make_unique<GoogleCalendarApi>(backend->network.get());
        backend->sync =
            std::make_unique<GoogleSync>(*backend->provider, *backend->api, *backend->cache);
        google->setSync(backend->sync.get());
    }
    backend->source = std::move(google);
    return backend;
}

/// The event commands: agenda, search, invites, add, edit, delete, respond and duplicate.
int runEvents(QCoreApplication &app, const QString &command, const QStringList &args,
              const QCommandLineParser &parser, bool sample)
{
    const auto value = [&parser](const char *name) -> std::optional<QString> {
        const QString option = QString::fromLatin1(name);
        return parser.isSet(option) ? std::optional(parser.value(option)) : std::nullopt;
    };
    const bool reading = command == u"agenda" || command == u"search" || command == u"invites";
    // Sample data and scripts that only read leave the user's settings alone.
    QTemporaryDir scratch;
    Settings settings(sample ? scratch.filePath(u"settings.ini"_s) : Settings::defaultPath());
    int code = 0;
    const std::unique_ptr<Backend> backend = openBackend(sample, !reading, code);
    if (!backend)
        return code;
    const QTimeZone zone = settings.timeZoneId().isEmpty()
                               ? QTimeZone::systemTimeZone()
                               : QTimeZone(settings.timeZoneId().toUtf8());
    cli::Commands commands(*backend->source, settings, out, err, QDateTime::currentDateTime(),
                           zone);
    commands.setColor(useColor());
    const bool json = parser.isSet(u"json"_s);

    if (command == u"agenda")
        return commands.agenda(parser.value(u"days"_s).toInt(), json);
    if (command == u"search")
        return commands.search(args.mid(1).join(u' '), json);
    if (command == u"invites")
        return commands.invites(json);

    const QString scope = parser.value(u"scope"_s);
    std::optional<int> result;
    const cli::Commands::Done done = [&result](int c) {
        result = c;
        finish(c);
    };
    if (command == u"add" && args.size() >= 2) {
        commands.add(args.mid(1).join(u' '), parser.value(u"calendar"_s), done);
    } else if (command == u"edit" && args.size() == 2) {
        cli::EditOptions options;
        options.title = value("title");
        options.where = value("where");
        options.notes = value("notes");
        options.start = value("start");
        options.end = value("end");
        if (parser.isSet(u"all-day"_s) || parser.isSet(u"timed"_s))
            options.allDay = parser.isSet(u"all-day"_s);
        options.repeat = value("repeat");
        if (const auto guests = value("guests"))
            options.guests = guests->split(u',', Qt::SkipEmptyParts);
        if (const auto video = value("video")) {
            const QString answer = video->toLower();
            if (!QStringList{u"on"_s, u"off"_s, u"yes"_s, u"no"_s}.contains(answer)) {
                err << QObject::tr("callie: --video is on or off") << "\n";
                return 2;
            }
            options.video = answer == u"on" || answer == u"yes";
        }
        commands.edit(args.at(1), options, scope, done);
    } else if (command == u"delete" && args.size() == 2) {
        commands.remove(args.at(1), scope, done);
    } else if (command == u"respond" && args.size() == 3) {
        commands.respond(args.at(1), args.at(2), scope, done);
    } else if (command == u"duplicate" && args.size() == 2) {
        commands.duplicate(args.at(1), value("start"), done);
    } else {
        err << QObject::tr("usage: callie %1; see callie --help").arg(command) << "\n";
        return 2;
    }
    // Sample data answers at once; Google answers over the network.
    return result ? *result : app.exec();
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
                       "  agenda     Upcoming events (default); --json for scripts, with ids\n"
                       "  search     Events with every word given\n"
                       "  invites    Invitations waiting for an answer\n"
                       "  add        Create an event: callie add \"Lunch tomorrow 12-1pm\"\n"
                       "  edit       Change an event: callie edit <id> --start \"friday 3pm\"\n"
                       "  delete     Delete an event\n"
                       "  respond    Answer an invitation: callie respond <id> yes|maybe|no\n"
                       "  duplicate  Copy an event, to --start if given\n"
                       "  accounts   List, add or remove calendar accounts\n"
                       "  calendars  List the calendars in each account\n"
                       "  logs       Show, follow (-f) or open (--open) the log files\n"
                       "  status     Sync state, keyring and configuration\n"
                       "  doctor     Details for a bug report; --report opens a new issue\n"
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
    QCommandLineOption verboseOption(QStringLiteral("verbose"),
                                     QStringLiteral("Print progress as well as warnings."));
    parser.addOption(verboseOption);
    QCommandLineOption reportOption(QStringLiteral("report"),
                                    QStringLiteral("With doctor: open a new GitHub issue."));
    parser.addOption(reportOption);
    for (const auto &[names, description, valueName] :
         std::initializer_list<std::tuple<QStringList, QString, QString>>{
             {{u"json"_s},
              u"With agenda, search and invites: print JSON, with the ids other commands take."_s,
              {}},
             {{u"calendar"_s}, u"With add: the calendar, by name or id."_s, u"name"_s},
             {{u"scope"_s},
              u"With edit, delete and respond on a repeating event: this, following or all."_s,
              u"which"_s},
             {{u"title"_s}, u"With edit: a new title."_s, u"text"_s},
             {{u"where"_s}, u"With edit: a new place."_s, u"text"_s},
             {{u"notes"_s}, u"With edit: new notes."_s, u"text"_s},
             {{u"start"_s},
              u"With edit and duplicate: a new start, such as \"2026-10-08 15:00\"."_s,
              u"when"_s},
             {{u"end"_s},
              u"With edit: a new end; for an all-day event, its last day."_s,
              u"when"_s},
             {{u"all-day"_s}, u"With edit: make it all day."_s, {}},
             {{u"timed"_s}, u"With edit: give it times."_s, {}},
             {{u"repeat"_s},
              u"With edit: none, daily, weekdays, weekly, monthly, monthlyWeekday or yearly."_s,
              u"rule"_s},
             {{u"guests"_s}, u"With edit: every guest's address, comma separated."_s, u"list"_s},
             {{u"video"_s}, u"With edit: on or off, to add or remove a video call."_s, u"on"_s},
         }) {
        parser.addOption(QCommandLineOption(names, description, valueName));
    }
    parser.process(app);
    logfile::install(QStringLiteral("callie"));
    logfile::setVerboseTerminal(parser.isSet(verboseOption) ||
                                !qEnvironmentVariableIsEmpty("QT_LOGGING_RULES"));

    const QStringList args = parser.positionalArguments();
    const QString command = args.isEmpty() ? QStringLiteral("agenda") : args.first();

    static const QStringList kEventCommands = {u"agenda"_s,  u"search"_s,   u"invites"_s,
                                               u"add"_s,     u"edit"_s,     u"delete"_s,
                                               u"respond"_s, u"duplicate"_s};
    if (kEventCommands.contains(command))
        return runEvents(app, command, args, parser, parser.isSet(sampleOption));

    if (command == QLatin1String("accounts"))
        return runAccounts(app, args);

    if (command == QLatin1String("doctor") && args.size() == 1)
        return runDoctor(app, parser.isSet(reportOption));

    if (command == QLatin1String("status") && args.size() == 1)
        return runStatus(app);

    if (command == QLatin1String("logs") && args.size() == 1)
        return runLogs(parser.isSet(followOption), parser.isSet(openOption));

    if (command == QLatin1String("sync") && args.size() == 1)
        return runSync(app);

    if (command == QLatin1String("calendars") && args.size() == 1)
        return runCalendars(app);

    if (command == QLatin1String("gui"))
        return QProcess::execute(QStringLiteral("callie-gui"), {});

    if (command == QLatin1String("daemon")) {
        err << QStringLiteral("callie: '%1' is not implemented yet.\n").arg(command);
        return 2;
    }

    err << QStringLiteral("callie: unknown command '%1'\n").arg(command);
    parser.showHelp(1);
}
