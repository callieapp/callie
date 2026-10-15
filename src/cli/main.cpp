#include "callie/AccountManager.h"
#include "callie/AccountStore.h"
#include "callie/CalDavClient.h"
#include "callie/CalDavSource.h"
#include "callie/CombinedSource.h"
#include "callie/ContactBook.h"
#include "callie/Diagnostics.h"
#include "callie/EventModel.h"
#include "callie/GoogleAuth.h"
#include "callie/GoogleCache.h"
#include "callie/GoogleCalendarApi.h"
#include "callie/GoogleSource.h"
#include "callie/GoogleSync.h"
#include "callie/GoogleTokenProvider.h"
#include "callie/GraphApi.h"
#include "callie/Ics.h"
#include "callie/LocalCalendars.h"
#include "callie/LogFile.h"
#include "callie/MicrosoftAuth.h"
#include "callie/MicrosoftSource.h"
#include "callie/MicrosoftTokenProvider.h"
#include "callie/QueuedSource.h"
#include "callie/SampleSource.h"
#include "callie/Settings.h"
#include "callie/Subscriptions.h"
#include "callie/Times.h"
#include "callie/TokenStore.h"
#include "callie/UpdateCheck.h"

#include "Commands.h"
#include "Completion.h"
#ifdef CALLIE_HAVE_TUI
#include "Tui.h"
#include "TuiState.h"
#endif
#include "UpdateHint.h"
#include "Upgrade.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include <csignal>
#include <termios.h>
#include <unistd.h>

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

/// The theme the CLI and TUI take their colors from: $CALLIE_THEME, as for the
/// app, else the one chosen in settings.
QString themeFor(const Settings &settings)
{
    const QString override = qEnvironmentVariable("CALLIE_THEME");
    return override.isEmpty() ? settings.theme() : override;
}

/// Colors from the user's theme when stdout is a terminal.
const cli::Paint &paint()
{
    static const cli::Paint paint = [] {
        const cli::Paint::Depth depth = cli::Paint::detect(STDOUT_FILENO);
        if (depth == cli::Paint::Depth::None)
            return cli::Paint();
        return cli::Paint(depth,
                          cli::Palette::forTheme(themeFor(Settings(Settings::defaultPath()))));
    }();
    return paint;
}

QString dim(const QString &s)
{
    return paint().dim(s);
}
QString bold(const QString &s)
{
    return paint().bold(s);
}
QString warn(const QString &s)
{
    return paint().danger(s);
}

const QString kGoogle = QStringLiteral("google");

/// The commands, for --help and for completion.
const QList<std::pair<QString, QString>> kCommands = {
    {u"tui"_s, u"Your calendar in the terminal, in the app's theme; the default on a terminal"_s},
    {u"agenda"_s, u"Upcoming events; --json for scripts, with ids. The default when piped"_s},
    {u"search"_s, u"Events with every word given"_s},
    {u"invites"_s, u"Invitations waiting for an answer"_s},
    {u"contacts"_s, u"People to invite whose name or address matches"_s},
    {u"add"_s, u"Create an event: callie add \"Lunch tomorrow 12-1pm\""_s},
    {u"import"_s, u"Add the events of an .ics file to a calendar"_s},
    {u"export"_s, u"Save a calendar as an .ics file: callie export Work work.ics"_s},
    {u"subscribe"_s, u"Subscribe to a calendar by its webcal:// or https:// link"_s},
    {u"unsubscribe"_s, u"Stop a subscription"_s},
    {u"edit"_s, u"Change an event: callie edit <id> --start \"friday 3pm\""_s},
    {u"delete"_s, u"Delete an event"_s},
    {u"respond"_s, u"Answer an invitation: callie respond <id> yes|maybe|no"_s},
    {u"duplicate"_s, u"Copy an event, to --start if given"_s},
    {u"accounts"_s, u"List, add, remove or rename calendar accounts"_s},
    {u"calendars"_s,
     u"List the calendars; make or remove one, here or on a CalDAV server; hide, show, rename, color or reset one"_s},
    {u"settings"_s, u"List settings, or read or change one: callie settings viMode true"_s},
    {u"logs"_s, u"Show, follow (-f) or open (--open) the log files"_s},
    {u"status"_s, u"Sync state, keyring and configuration"_s},
    {u"doctor"_s, u"Details for a bug report; --report opens a new issue"_s},
    {u"sync"_s, u"Refresh all accounts now"_s},
    {u"upgrade"_s, u"Install the latest release, or the one given: callie upgrade 0.3.1"_s},
    {u"downgrade"_s, u"Go back to the release before this one, or the one given"_s},
    {u"completions"_s, u"Print the shell completion script: bash, zsh or fish"_s},
    {u"daemon"_s, u"Run background sync and notifications"_s},
    {u"gui"_s, u"Launch the desktop app"_s},
};

/// After a command on a terminal, says once a newer release is out. GitHub is
/// asked at most once a day, by a copy of callie left running on its own, so no
/// command waits for it.
class HintAfterwards
{
public:
    HintAfterwards(QString command, bool json) : m_command(std::move(command)), m_json(json) {}
    HintAfterwards(const HintAfterwards &) = delete;
    HintAfterwards &operator=(const HintAfterwards &) = delete;

    ~HintAfterwards()
    {
        UpdateCheck check;
        const cli::UpdateHint hint = cli::UpdateHint::after(
            m_command, m_json, ::isatty(STDERR_FILENO),
            Settings(Settings::defaultPath()).checkForUpdates(), check,
            QVersionNumber::fromString(QCoreApplication::applicationVersion()));
        if (!hint.line.isEmpty())
            err << dim(hint.line) << "\n" << Qt::flush;
        if (hint.ask) {
            check.markAsked();
            QProcess asker;
            asker.setProgram(QCoreApplication::applicationFilePath());
            asker.setArguments({u"update-check"_s});
            asker.setStandardOutputFile(QProcess::nullDevice());
            asker.setStandardErrorFile(QProcess::nullDevice());
            asker.startDetached();
        }
    }

private:
    QString m_command;
    bool m_json;
};

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
        err << QObject::tr("No accounts. Add one with: callie accounts add google, callie "
                           "accounts add microsoft, or callie accounts add caldav <server> <user>")
            << "\n";
        return 0;
    }
    for (const Account &account : accounts) {
        out << account.provider << "\t" << account.id;
        if (!account.server.isEmpty())
            out << "\t" << account.server;
        out << "\n";
    }
    return 0;
}

/// The terminal as it was before a password was asked for, put back on Ctrl-C too.
termios g_terminal{};

void restoreTerminal(int signal)
{
    tcsetattr(STDIN_FILENO, TCSANOW, &g_terminal);
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}

/// The password, asked for without showing it when a person is typing, or read
/// as the first line of what is piped in.
QString readPassword(const QString &prompt)
{
    const bool typing = isatty(STDIN_FILENO);
    if (typing) {
        err << prompt;
        err.flush();
        tcgetattr(STDIN_FILENO, &g_terminal);
        std::signal(SIGINT, restoreTerminal);
        std::signal(SIGTERM, restoreTerminal);
        termios quiet = g_terminal;
        quiet.c_lflag &= ~tcflag_t(ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &quiet);
    }
    QTextStream in(stdin);
    const QString password = in.readLine();
    if (typing) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_terminal);
        std::signal(SIGINT, SIG_DFL);
        std::signal(SIGTERM, SIG_DFL);
        err << "\n";
        err.flush();
    }
    return password;
}

int runAccountsAddCalDav(QCoreApplication &app, const QString &server, const QString &user)
{
    const QUrl url(server.contains(u"://"_s) ? server : u"https://"_s + server);
    const QString password = readPassword(QObject::tr("App password for %1: ").arg(user));
    if (password.isEmpty()) {
        err << QObject::tr("callie: no password given") << "\n";
        return 2;
    }
    QNetworkAccessManager network;
    CalDavClient client(&network);
    KeychainTokenStore tokens;
    AccountStore store(AccountStore::defaultPath());
    AccountManager manager(tokens, store);
    QObject::connect(&manager, &AccountManager::connected, [](const Account &account) {
        out << account.id << "\n";
        err << QObject::tr("Bring its calendars down with: callie sync") << "\n";
        finish(0);
    });
    QObject::connect(&manager, &AccountManager::failed, [](const QString &message) {
        err << QStringLiteral("callie: %1\n").arg(message);
        finish(1);
    });
    QTimer::singleShot(0, &manager, [&] { manager.connectCalDav(client, url, user, password); });
    return app.exec();
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

int runAccountsAddMicrosoft(QCoreApplication &app)
{
    const MicrosoftClientConfig client = MicrosoftClientConfig::resolve();
    if (!client.isValid()) {
        err << QObject::tr(
                   "callie: this build has no Microsoft app; set CALLIE_MICROSOFT_CLIENT_ID "
                   "to the application id")
            << "\n";
        return 1;
    }
    MicrosoftAuth auth(client);
    QNetworkAccessManager network;
    GraphApi graph(&network);
    KeychainTokenStore tokens;
    AccountStore store(AccountStore::defaultPath());
    AccountManager manager(tokens, store);
    QObject::connect(&auth, &MicrosoftAuth::authorizeUrlReady, [](const QUrl &url) {
        const QString link = url.toString(QUrl::FullyEncoded);
        err << QObject::tr("Opening your browser to sign in to Microsoft. If it does not open, "
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
    QTimer::singleShot(0, &manager, [&] { manager.connectMicrosoft(auth, graph); });
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
    ContactBook contacts(ContactBook::defaultPath());
    manager.setContacts(&contacts);
    CalDavSource caldav(CalDavSource::defaultRoot(), nullptr, nullptr);
    manager.setCalDav(&caldav);
    MicrosoftSource microsoft(MicrosoftSource::defaultRoot(), nullptr, nullptr);
    manager.setMicrosoft(&microsoft);

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
                // Google's choice, which only decides how a calendar starts in Callie.
                if (!calendar.selected)
                    out << "  " << dim(QObject::tr("hidden in Google"));
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
    // Subscribed calendars first, since they need no account.
    QNetworkAccessManager network;
    Subscriptions subscriptions(Subscriptions::defaultRoot(), &network);
    bool failed = false;
    if (!subscriptions.calendars().isEmpty()) {
        QEventLoop fetching;
        QObject::connect(&subscriptions, &Subscriptions::fetched, &fetching, &QEventLoop::quit);
        subscriptions.fetchAll();
        if (subscriptions.syncing())
            fetching.exec();
        for (const QVariant &entry : subscriptions.syncReport()) {
            const QVariantMap feed = entry.toMap();
            if (const QString error = feed.value(u"error"_s).toString(); !error.isEmpty()) {
                err << QStringLiteral("callie: %1: %2\n")
                           .arg(feed.value(u"account"_s).toString(), error);
                failed = true;
            }
        }
    }

    AccountStore store(AccountStore::defaultPath());
    QList<Account> accounts;
    if (!store.load(accounts)) {
        err << QStringLiteral("callie: %1\n").arg(store.errorString());
        return 1;
    }
    // Then CalDAV accounts, sending what waits and bringing down what changed.
    KeychainTokenStore tokens;
    CalDavSource caldav(CalDavSource::defaultRoot(), &network, &tokens);
    QList<CalDavSource::Account> calDavAccounts;
    for (const Account &account : std::as_const(accounts)) {
        if (account.provider == u"caldav")
            calDavAccounts << CalDavSource::Account{account.id, QUrl(account.server), account.user};
    }
    if (!calDavAccounts.isEmpty()) {
        caldav.setAccounts(calDavAccounts);
        QEventLoop syncing;
        QObject::connect(&caldav, &CalDavSource::synced, &syncing, &QEventLoop::quit);
        caldav.refresh();
        if (caldav.syncing())
            syncing.exec();
        for (const QVariant &entry : caldav.syncReport()) {
            const QVariantMap account = entry.toMap();
            if (const QString error = account.value(u"error"_s).toString(); !error.isEmpty()) {
                err << QStringLiteral("callie: %1: %2\n")
                           .arg(account.value(u"account"_s).toString(), error);
                failed = true;
            }
        }
    }

    // Then Microsoft accounts, the same way.
    QStringList microsoftAccounts;
    for (const Account &account : std::as_const(accounts)) {
        if (account.provider == u"microsoft")
            microsoftAccounts << account.id;
    }
    const MicrosoftClientConfig microsoftClient = MicrosoftClientConfig::resolve();
    MicrosoftTokenProvider microsoftTokens(microsoftClient, tokens);
    GraphApi graph(&network);
    MicrosoftSource microsoft(MicrosoftSource::defaultRoot(), &graph, &microsoftTokens);
    if (!microsoftAccounts.isEmpty() && !microsoftClient.isValid()) {
        err << QObject::tr("callie: this build has no Microsoft app, so Microsoft accounts "
                           "cannot sync; set CALLIE_MICROSOFT_CLIENT_ID")
            << "\n";
        failed = true;
    } else if (!microsoftAccounts.isEmpty()) {
        microsoft.setAccounts(microsoftAccounts);
        QEventLoop syncing;
        QObject::connect(&microsoft, &MicrosoftSource::synced, &syncing, &QEventLoop::quit);
        microsoft.refresh();
        if (microsoft.syncing())
            syncing.exec();
        for (const QVariant &entry : microsoft.syncReport()) {
            const QVariantMap account = entry.toMap();
            if (const QString error = account.value(u"error"_s).toString(); !error.isEmpty()) {
                err << QStringLiteral("callie: %1: %2\n")
                           .arg(account.value(u"account"_s).toString(), error);
                failed = true;
            }
        }
    }

    accounts.removeIf([](const Account &a) { return a.provider != kGoogle; });
    if (accounts.isEmpty()) {
        if (subscriptions.calendars().isEmpty() && calDavAccounts.isEmpty() &&
            microsoftAccounts.isEmpty())
            err << QObject::tr("No accounts. Add one with: callie accounts add google") << "\n";
        return failed ? 1 : 0;
    }

    GoogleCache cache(GoogleCache::defaultPath());
    if (!cache.open()) {
        err << QStringLiteral("callie: %1\n").arg(cache.errorString());
        return 1;
    }
    const std::optional<GoogleClientConfig> client = googleClient();
    if (!client)
        return 1;
    GoogleTokenProvider provider(*client, tokens);
    GoogleCalendarApi api(&network);
    GoogleSync sync(provider, api, cache);

    qsizetype remaining = accounts.size();
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

/// CalDAV calendars as `callie calendars new` and `remove` reach them: the
/// accounts, signed in from the keyring, with their copies.
struct ServerCalendars
{
    QList<CalDavSource::Account> accounts;
    QNetworkAccessManager network;
    KeychainTokenStore tokens;
    CalDavSource source{CalDavSource::defaultRoot(), &network, &tokens};

    ServerCalendars()
    {
        QList<Account> all;
        AccountStore(AccountStore::defaultPath()).load(all);
        for (const Account &account : std::as_const(all)) {
            if (account.provider == u"caldav")
                accounts << CalDavSource::Account{account.id, QUrl(account.server), account.user};
        }
        source.setAccounts(accounts);
    }
};

/// `callie calendars new <name> [--account <id>]` and `callie calendars remove
/// <calendar>`, for calendars on this computer or on a CalDAV server.
int runCalendarChange(const QString &action, const QString &name, const QString &accountId,
                      bool yes)
{
    if (name.trimmed().isEmpty()) {
        err << QObject::tr("usage: callie calendars %1 <name>").arg(action) << "\n";
        return 2;
    }
    LocalCalendars local(LocalCalendars::defaultRoot());
    QString error;
    if (action == u"new" && accountId.isEmpty()) {
        const QString id = local.create(name, {}, &error);
        if (id.isEmpty()) {
            err << QStringLiteral("callie: %1\n").arg(error);
            return 1;
        }
        out << id << "\n";
        return 0;
    }
    ServerCalendars server;
    if (action == u"new") {
        const auto account =
            std::ranges::find(server.accounts, accountId, &CalDavSource::Account::id);
        if (account == server.accounts.cend()) {
            err << QObject::tr("callie: no CalDAV account is called %1; callie accounts lists them")
                       .arg(accountId)
                << "\n";
            return 1;
        }
        std::optional<std::pair<QString, QString>> made;
        QEventLoop waiting;
        server.source.createCalendar(accountId, name, {},
                                     [&made, &waiting](const QString &id, const QString &failed) {
                                         made = {id, failed};
                                         waiting.quit();
                                     });
        if (!made)
            waiting.exec();
        if (!made->second.isEmpty()) {
            err << QStringLiteral("callie: %1\n").arg(made->second);
            return 1;
        }
        out << made->first << "\n";
        return 0;
    }
    QTextStream answers(stdin);
    return cli::Commands::removeCalendar(local, server.source, name, yes,
                                         isatty(STDIN_FILENO) ? &answers : nullptr, out, err);
}

/// `callie subscribe <link> [name]`, fetched at once, and `callie unsubscribe
/// <calendar>`.
int runSubscription(const QString &command, const QString &target, const QString &name)
{
    if (target.isEmpty()) {
        err << (command == u"subscribe" ? QObject::tr("usage: callie subscribe <link> [name]")
                                        : QObject::tr("usage: callie unsubscribe <calendar>"))
            << "\n";
        return 2;
    }
    QNetworkAccessManager network;
    Subscriptions subscriptions(Subscriptions::defaultRoot(), &network);
    QString error;
    if (command == u"subscribe") {
        QEventLoop fetching;
        QObject::connect(&subscriptions, &Subscriptions::fetched, &fetching, &QEventLoop::quit);
        const QString id = subscriptions.add(target, name, &error);
        if (id.isEmpty()) {
            err << QStringLiteral("callie: %1\n").arg(error);
            return 1;
        }
        if (subscriptions.syncing())
            fetching.exec();
        const QList<CalendarInfo> calendars = subscriptions.calendars();
        const QVariantList report = subscriptions.syncReport();
        for (qsizetype i = 0; i < calendars.size(); ++i) {
            if (calendars.at(i).id != id)
                continue;
            // Kept, so a link that is down for now works once it is back.
            if (const QString problem = report.value(i).toMap().value(u"error"_s).toString();
                !problem.isEmpty()) {
                err << QStringLiteral("callie: %1: %2\n").arg(calendars.at(i).displayName, problem);
                return 1;
            }
            out << QObject::tr("Subscribed to %1.").arg(calendars.at(i).displayName) << "\n";
        }
        return 0;
    }
    QStringList ids;
    for (const CalendarInfo &calendar : subscriptions.calendars()) {
        if (calendar.id == target ||
            calendar.displayName.compare(target, Qt::CaseInsensitive) == 0 ||
            subscriptions.link(calendar.id) == target)
            ids << calendar.id;
    }
    if (ids.size() != 1) {
        err << (ids.isEmpty() ? QObject::tr("callie: no subscription is called %1").arg(target)
                              : QObject::tr("callie: more than one is called %1; give its id: %2")
                                    .arg(target, ids.join(u", "_s)))
            << "\n";
        return ids.isEmpty() ? 1 : 2;
    }
    if (!subscriptions.remove(ids.first(), &error)) {
        err << QStringLiteral("callie: %1\n").arg(error);
        return 1;
    }
    out << QObject::tr("Unsubscribed from %1.").arg(target) << "\n";
    return 0;
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
    const LocalCalendars local(LocalCalendars::defaultRoot());
    for (const CalendarInfo &calendar : local.calendars())
        out << LocalCalendars::kAccount << "\t" << calendar.id << "\t" << calendar.displayName
            << "\n";
    const Subscriptions subscriptions(Subscriptions::defaultRoot(), nullptr);
    for (const CalendarInfo &calendar : subscriptions.calendars())
        out << Subscriptions::kAccount << "\t" << calendar.id << "\t" << calendar.displayName
            << "\n";
    accounts.removeIf([](const Account &a) { return a.provider != kGoogle; });
    if (accounts.isEmpty()) {
        if (local.calendars().isEmpty() && subscriptions.calendars().isEmpty())
            err << QObject::tr("No calendars. Add an account with: callie accounts add google, "
                               "or make a calendar with: callie calendars new <name>")
                << "\n";
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
    std::unique_ptr<GoogleSource> google;
    std::unique_ptr<LocalCalendars> local;
    std::unique_ptr<Subscriptions> subscriptions;
    std::unique_ptr<CalDavSource> caldav;
    std::unique_ptr<MicrosoftTokenProvider> microsoftTokens;
    std::unique_ptr<GraphApi> graph;
    std::unique_ptr<MicrosoftSource> microsoft;
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
    QList<CalDavSource::Account> calDavAccounts;
    QStringList microsoftAccounts;
    for (const Account &account : std::as_const(backend->accounts)) {
        if (account.provider == u"caldav")
            calDavAccounts << CalDavSource::Account{account.id, QUrl(account.server), account.user};
        if (account.provider == u"microsoft")
            microsoftAccounts << account.id;
    }
    backend->accounts.removeIf([](const Account &a) { return a.provider != kGoogle; });
    // Changes need the network and the keyring; reading needs neither.
    if (writing) {
        backend->tokens = std::make_unique<KeychainTokenStore>();
        backend->network = std::make_unique<QNetworkAccessManager>();
    }
    backend->local = std::make_unique<LocalCalendars>(LocalCalendars::defaultRoot());
    // Read as last fetched; `callie sync` fetches them.
    backend->subscriptions = std::make_unique<Subscriptions>(Subscriptions::defaultRoot(), nullptr);
    backend->caldav = std::make_unique<CalDavSource>(CalDavSource::defaultRoot(),
                                                     backend->network.get(), backend->tokens.get());
    backend->caldav->setAccounts(calDavAccounts);
    if (writing) {
        backend->microsoftTokens = std::make_unique<MicrosoftTokenProvider>(
            MicrosoftClientConfig::resolve(), *backend->tokens);
        backend->graph = std::make_unique<GraphApi>(backend->network.get());
    }
    backend->microsoft = std::make_unique<MicrosoftSource>(
        MicrosoftSource::defaultRoot(), backend->graph.get(), backend->microsoftTokens.get());
    backend->microsoft->setAccounts(microsoftAccounts);
    const bool others =
        !backend->local->calendars().isEmpty() || !backend->subscriptions->calendars().isEmpty() ||
        !backend->caldav->calendars().isEmpty() || !backend->microsoft->calendars().isEmpty();
    QList<CalendarSource *> sources;
    if (!backend->accounts.isEmpty()) {
        backend->cache = std::make_unique<GoogleCache>(GoogleCache::defaultPath());
        if (!backend->cache->open()) {
            err << QStringLiteral("callie: %1\n").arg(backend->cache->errorString());
            code = 1;
            return nullptr;
        }
        backend->google = std::make_unique<GoogleSource>(*backend->cache, backend->accounts);
        // Without a Google client, changes to Google calendars say so when made.
        const GoogleClientConfig client = GoogleClientConfig::resolve();
        if (writing && client.isValid()) {
            backend->provider = std::make_unique<GoogleTokenProvider>(client, *backend->tokens);
            backend->api = std::make_unique<GoogleCalendarApi>(backend->network.get());
            backend->sync =
                std::make_unique<GoogleSync>(*backend->provider, *backend->api, *backend->cache);
            backend->google->setSync(backend->sync.get());
        }
        sources << backend->google.get();
    }
    if ((!backend->google || backend->google->calendars().isEmpty()) && !others) {
        err << (backend->accounts.isEmpty() && calDavAccounts.isEmpty() &&
                        microsoftAccounts.isEmpty()
                    ? QObject::tr("No calendars. Add an account with: callie accounts add google, "
                                  "or make a calendar with: callie calendars new <name>")
                    : QObject::tr("Nothing synced yet. Run: callie sync"))
            << "\n";
        return nullptr;
    }
    sources << backend->microsoft.get() << backend->caldav.get() << backend->local.get()
            << backend->subscriptions.get();
    auto combined = std::make_unique<CombinedSource>();
    combined->setSources(sources);
    backend->source = std::move(combined);
    return backend;
}

/// Sends the changes made to one kind of server calendar before the command
/// ends, giving up after a while; what is left goes with the next sync. False
/// when the server turned one down.
template<typename Source>
bool sendChanges(Source *source)
{
    if (!source || source->waitingChanges().isEmpty())
        return true;
    bool taken = true;
    const QMetaObject::Connection refused =
        QObject::connect(source, &CalendarSource::errorOccurred, [&taken](const QString &message) {
            err << QObject::tr("callie: not made: %1").arg(message) << "\n";
            taken = false;
        });
    QEventLoop sending;
    QObject::connect(source, &Source::synced, &sending, &QEventLoop::quit);
    QTimer::singleShot(std::chrono::seconds(15), &sending, &QEventLoop::quit);
    source->refresh();
    if (source->syncing())
        sending.exec();
    QObject::disconnect(refused);
    for (const QString &waiting : source->waitingChanges())
        err << QObject::tr("callie: %1; they go with the next sync").arg(waiting) << "\n";
    return taken;
}

/// Sends what waits for CalDAV and Microsoft calendars; false when either
/// turned a change down.
bool sendServerChanges(const Backend &backend)
{
    const bool caldav = sendChanges(backend.caldav.get());
    const bool microsoft = sendChanges(backend.microsoft.get());
    return caldav && microsoft;
}

/// The event commands: agenda, search, invites, add, edit, delete, respond and duplicate.
int runEvents(QCoreApplication &app, const QString &command, const QStringList &args,
              const QCommandLineParser &parser, bool sample)
{
    const auto value = [&parser](const char *name) -> std::optional<QString> {
        const QString option = QString::fromLatin1(name);
        return parser.isSet(option) ? std::optional(parser.value(option)) : std::nullopt;
    };
    // Renaming and recoloring reach CalDAV servers.
    const bool looks = command == u"calendars" &&
                       QStringList{u"rename"_s, u"color"_s, u"reset"_s}.contains(args.value(1));
    const bool reading = command == u"agenda" || command == u"search" || command == u"invites" ||
                         command == u"contacts" || command == u"export" ||
                         (command == u"calendars" && !looks) || command == u"accounts";
    // Sample data and scripts that only read leave the user's settings alone.
    QTemporaryDir scratch;
    Settings settings(sample ? scratch.filePath(u"settings.ini"_s) : Settings::defaultPath());
    // Sample data can be shown at a fixed time, for screenshots.
    QDateTime fixedNow;
    if (parser.isSet(u"now"_s)) {
        fixedNow = QDateTime::fromString(parser.value(u"now"_s), Qt::ISODate);
        if (!sample || !fixedNow.isValid()) {
            err << QObject::tr("usage: --now goes with --sample, as in --now 2026-10-07T13:40")
                << "\n";
            return 2;
        }
    }
    const auto now = [fixedNow] {
        return fixedNow.isValid() ? fixedNow : QDateTime::currentDateTime();
    };
    int code = 0;
    const std::unique_ptr<Backend> backend = openBackend(sample, !reading, code);
    if (!backend)
        return code;
    if (sample)
        static_cast<SampleSource *>(backend->source.get())->setNow(now);
    const QTimeZone zone = settings.timeZoneId().isEmpty()
                               ? QTimeZone::systemTimeZone()
                               : QTimeZone(settings.timeZoneId().toUtf8());
    cli::Commands commands(*backend->source, settings, out, err, now(), zone);
    commands.setPaint(paint());
    const bool json = parser.isSet(u"json"_s);

    if (command == u"calendars") {
        if (backend->caldav)
            backend->caldav->followLooks(&settings);
        const int looked =
            commands.calendarLook(args.value(1), args.value(2), args.mid(3).join(u' '));
        return sendServerChanges(*backend) ? looked : 1;
    }
    if (command == u"accounts")
        return commands.renameAccount(args.value(2), args.mid(3).join(u' '));
    if (command == u"agenda")
        return commands.agenda(parser.value(u"days"_s).toInt(), json);
    if (command == u"tui") {
#ifdef CALLIE_HAVE_TUI
        // Changes show at once and can be taken back while held, as in the app.
        // The queue lives only while the TUI runs, apart from the app's.
        QueuedSource queued(*backend->source, QString());
        queued.setNow(now);
        cli::TuiState state(queued, settings, now);
        queued.refresh();
        const int code = cli::runTui(state, settings, cli::Palette::forTheme(themeFor(settings)));
        // What is still held goes now, given a moment to arrive. A change the
        // calendar turns down is dropped, so it is reported here.
        QStringList refused;
        QObject::connect(&queued, &CalendarSource::errorOccurred,
                         [&refused](const QString &message) { refused.append(message); });
        queued.sendAll();
        QElapsedTimer waited;
        waited.start();
        while (!queued.changes().isEmpty() && waited.elapsed() < 15000)
            QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 100);
        for (const QString &message : std::as_const(refused))
            err << QObject::tr("callie: not made: %1").arg(message) << "\n";
        for (const PendingChange &change : queued.changes())
            err << QObject::tr("callie: not sent: %1").arg(change.describe()) << "\n";
        const bool sent = sendServerChanges(*backend);
        return sent && queued.changes().isEmpty() && refused.isEmpty() ? code : 1;
#else
        err << QObject::tr("callie: this build has no TUI; try: callie agenda") << "\n";
        return 1;
#endif
    }
    if (command == u"export")
        return commands.exportCalendar(args.value(1), args.value(2));
    if (command == u"search")
        return commands.search(args.mid(1).join(u' '), json);
    if (command == u"invites")
        return commands.invites(json);
    if (command == u"contacts") {
        ContactBook book(sample ? QString() : ContactBook::defaultPath());
        return commands.contacts(book, args.mid(1).join(u' '), json);
    }

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
    const int ended = result ? *result : app.exec();
    return sendServerChanges(*backend) ? ended : 1;
}

int runAccounts(QCoreApplication &app, const QStringList &args)
{
    const QString action = args.value(1, QStringLiteral("list"));
    const QString provider = args.value(2);

    if (action == QLatin1String("list") && args.size() <= 2)
        return runAccountsList();
    if (action == QLatin1String("add") && provider == kGoogle && args.size() == 3)
        return runAccountsAddGoogle(app);
    if (action == QLatin1String("add") && provider == u"microsoft" && args.size() == 3)
        return runAccountsAddMicrosoft(app);
    if (action == QLatin1String("add") && provider == u"caldav" && args.size() == 5)
        return runAccountsAddCalDav(app, args.at(3), args.at(4));
    if (action == QLatin1String("remove") &&
        (provider == kGoogle || provider == u"caldav" || provider == u"microsoft") &&
        args.size() == 4)
        return runAccountsRemove(app, Account{provider, args.at(3)});

    err << QObject::tr(
               "usage: callie accounts [list | add google | add microsoft | add caldav <server> "
               "<user> | remove google|microsoft|caldav <id> | rename <id> <name>]")
        << "\n";
    return 2;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("callie"));
    app.setApplicationVersion(QStringLiteral(CALLIE_VERSION));

    QCommandLineParser parser;
    QString description = u"Callie: an elegant calendar for Linux.\n\nCommands:"_s;
    qsizetype width = 0;
    for (const auto &[name, about] : kCommands)
        width = std::max(width, name.size());
    for (const auto &[name, about] : kCommands)
        description += u"\n  "_s + name.leftJustified(width + 2) + about;
    parser.setApplicationDescription(description);
    // Kept for completion too, since the parser does not list them.
    QList<QCommandLineOption> options{parser.addHelpOption(), parser.addVersionOption()};
    const auto addOption = [&parser, &options](const QCommandLineOption &option) {
        parser.addOption(option);
        options.append(option);
    };
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("Command to run."));

    QCommandLineOption daysOption({QStringLiteral("d"), QStringLiteral("days")},
                                  QStringLiteral("Days of agenda to show."), QStringLiteral("n"),
                                  QStringLiteral("7"));
    addOption(daysOption);
    QCommandLineOption sampleOption(
        QStringLiteral("sample"), QStringLiteral("Show a made-up week instead of your calendars."));
    addOption(sampleOption);
    QCommandLineOption nowOption(QStringLiteral("now"),
                                 QStringLiteral("With --sample: pretend it is this local time."),
                                 QStringLiteral("time"));
    nowOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(nowOption);
    QCommandLineOption followOption({QStringLiteral("f"), QStringLiteral("follow")},
                                    QStringLiteral("With logs: keep printing new lines."));
    addOption(followOption);
    QCommandLineOption openOption(QStringLiteral("open"),
                                  QStringLiteral("With logs: open the folder."));
    addOption(openOption);
    QCommandLineOption verboseOption(QStringLiteral("verbose"),
                                     QStringLiteral("Print progress as well as warnings."));
    addOption(verboseOption);
    QCommandLineOption reportOption(QStringLiteral("report"),
                                    QStringLiteral("With doctor: open a new GitHub issue."));
    addOption(reportOption);
    QCommandLineOption forceOption(
        QStringLiteral("force"),
        QStringLiteral("With upgrade: install the release even when it is the one installed."));
    addOption(forceOption);
    QCommandLineOption dryRunOption(
        QStringLiteral("dry-run"),
        QStringLiteral("With upgrade and downgrade: say what would happen, and change nothing."));
    addOption(dryRunOption);
    for (
        const auto &[names, description, valueName] :
        std::initializer_list<std::tuple<QStringList, QString, QString>>{
            {{u"json"_s},
             u"With agenda, search, invites and contacts: print JSON, with ids for other commands."_s,
             {}},
            {{u"calendar"_s}, u"With add or import: the calendar, by name or id."_s, u"name"_s},
            {{u"account"_s},
             u"With calendars new: make it on this CalDAV account's server."_s,
             u"id"_s},
            {{u"yes"_s}, u"With calendars remove: delete a CalDAV calendar without asking."_s, {}},
            {{u"scope"_s},
             u"With edit, delete and respond on a repeating event: this, following or all."_s,
             u"which"_s},
            {{u"title"_s}, u"With edit: a new title."_s, u"text"_s},
            {{u"where"_s}, u"With edit: a new place."_s, u"text"_s},
            {{u"notes"_s}, u"With edit: new notes."_s, u"text"_s},
            {{u"start"_s},
             u"With edit and duplicate: a new start, such as \"2026-10-08 15:00\"."_s,
             u"when"_s},
            {{u"end"_s}, u"With edit: a new end; for an all-day event, its last day."_s, u"when"_s},
            {{u"all-day"_s}, u"With edit: make it all day."_s, {}},
            {{u"timed"_s}, u"With edit: give it times."_s, {}},
            {{u"repeat"_s},
             u"With edit: none, daily, weekdays, weekly, monthly, monthlyWeekday or yearly."_s,
             u"rule"_s},
            {{u"guests"_s}, u"With edit: every guest's address, comma separated."_s, u"list"_s},
            {{u"video"_s}, u"With edit: on or off, to add or remove a video call."_s, u"on"_s},
        }) {
        addOption(QCommandLineOption(names, description, valueName));
    }
    // Run by the shell completion scripts with the words typed so far, which
    // are not parsed as options since they are still being typed.
    if (app.arguments().value(1) == u"__complete") {
        // The shells hand over the line as typed, up to the cursor.
        const QStringList words = app.arguments().value(2) == u"--line"
                                      ? cli::Completion::wordsOf(app.arguments().value(3))
                                      : app.arguments().mid(2);
        std::unique_ptr<Backend> backend;
        const auto source = [&backend, &words]() -> CalendarSource * {
            int code = 0;
            if (!backend)
                backend = openBackend(words.contains(u"--sample"_s), false, code);
            return backend ? backend->source.get() : nullptr;
        };
        const Settings settings(Settings::defaultPath());
        const auto events = [&source, &settings](bool invitations) {
            QList<cli::Candidate> found;
            CalendarSource *calendar = source();
            if (!calendar)
                return found;
            // Times in the zone the user chose, as every command shows them.
            const Times &times = *settings.times();
            const QDateTime now = QDateTime::currentDateTime();
            for (const Event &e :
                 calendar->eventsBetween(now.addDays(-1), now.addDays(30), times.zone())) {
                if (invitations && (e.responseStatus != u"needsAction" || !e.canRespond))
                    continue;
                const QString day = QLocale().toString(times.date(e.start).date(), u"ddd d MMM"_s);
                found.append(
                    {cli::Commands::reference(e),
                     (e.allDay ? day : day + u' ' + times.time(e.start)) + u' ' + e.summary});
            }
            return found;
        };
        cli::Completion::Sources sources;
        sources.calendars = [&source] {
            QList<cli::Candidate> found;
            if (CalendarSource *calendar = source()) {
                for (const CalendarInfo &info : calendar->calendars())
                    found.append({info.displayName, {}});
            }
            return found;
        };
        sources.events = [events] { return events(false); };
        sources.invitations = [events] { return events(true); };
        sources.accounts = [] {
            QList<Account> accounts;
            AccountStore(AccountStore::defaultPath()).load(accounts);
            QList<cli::Candidate> found;
            for (const Account &account : std::as_const(accounts))
                found.append({account.id, account.provider});
            return found;
        };
        sources.settings = [&settings] { return cli::Commands::settingNames(settings); };
        QList<cli::Candidate> commands;
        for (const auto &[name, about] : kCommands)
            commands.append({name, about});
        QList<cli::Completion::Option> named;
        for (const QCommandLineOption &option : std::as_const(options)) {
            for (const QString &name : option.names())
                named.append({name, option.description(), !option.valueName().isEmpty()});
        }
        const cli::Completion completion(commands, named, sources);
        for (const cli::Candidate &candidate : completion.complete(words))
            out << candidate.value
                << (candidate.about.isEmpty() ? QString() : u'\t' + candidate.about) << "\n";
        return 0;
    }

    parser.process(app);
    logfile::install(QStringLiteral("callie"));
    logfile::setVerboseTerminal(parser.isSet(verboseOption) ||
                                !qEnvironmentVariableIsEmpty("QT_LOGGING_RULES"));

    const QStringList args = parser.positionalArguments();
    // On its own in a terminal, `callie` opens the TUI; piped, asked for the
    // agenda's options, or in a build without a TUI, it lists the agenda.
#ifdef CALLIE_HAVE_TUI
    const bool interactive = ::isatty(STDIN_FILENO) && ::isatty(STDOUT_FILENO) &&
                             !parser.isSet(u"json"_s) && !parser.isSet(daysOption);
#else
    const bool interactive = false;
#endif
    const QString command = !args.isEmpty() ? args.first() : interactive ? u"tui"_s : u"agenda"_s;
    const HintAfterwards hint(command, parser.isSet(u"json"_s));

    if (command == u"completions") {
        const QHash<QString, QString> files = {
            {u"bash"_s, u"callie.bash"_s}, {u"zsh"_s, u"_callie"_s}, {u"fish"_s, u"callie.fish"_s}};
        QFile script(u":/completions/"_s + files.value(args.value(1), u"none"_s));
        if (args.size() != 2 || !script.open(QIODevice::ReadOnly)) {
            err << QObject::tr("usage: callie completions bash|zsh|fish") << "\n";
            return 2;
        }
        out << script.readAll();
        return 0;
    }

    // Run by UpdateHint, on its own, to learn the latest release.
    if (command == u"update-check" && args.size() == 1) {
        UpdateCheck check;
        if (!check.wanted(Settings(Settings::defaultPath()).checkForUpdates()))
            return 0;
        QObject::connect(&check, &UpdateCheck::checked, &app, &QCoreApplication::quit);
        check.check();
        return app.exec();
    }

    static const QStringList kEventCommands = {
        u"agenda"_s, u"search"_s,  u"invites"_s,   u"add"_s,      u"edit"_s,
        u"delete"_s, u"respond"_s, u"duplicate"_s, u"contacts"_s, u"tui"_s};
    if (kEventCommands.contains(command))
        return runEvents(app, command, args, parser, parser.isSet(sampleOption));

    // Renaming changes only how Callie shows the account, so it needs no sign-in.
    if (command == QLatin1String("accounts") && args.value(1) == u"rename" && args.size() >= 3)
        return runEvents(app, command, args, parser, parser.isSet(sampleOption));
    if (command == QLatin1String("accounts"))
        return runAccounts(app, args);
    if (command == QLatin1String("settings") && args.size() > 3) {
        err << QObject::tr("usage: callie settings [name [value]]") << "\n";
        return 2;
    }
    if (command == QLatin1String("settings")) {
        Settings settings(Settings::defaultPath());
        return cli::Commands::settings(settings, out, err, args.value(1),
                                       args.size() == 3 ? std::optional(args.at(2)) : std::nullopt);
    }

    if (command == QLatin1String("doctor") && args.size() == 1)
        return runDoctor(app, parser.isSet(reportOption));

    if (command == QLatin1String("status") && args.size() == 1)
        return runStatus(app);

    if (command == QLatin1String("logs") && args.size() == 1)
        return runLogs(parser.isSet(followOption), parser.isSet(openOption));

    if (command == QLatin1String("sync") && args.size() == 1)
        return runSync(app);

    if ((command == u"upgrade" || command == u"downgrade") && args.size() <= 2) {
        cli::Upgrade upgrade(out, err);
        cli::Upgrade::Options options;
        options.downgrade = command == u"downgrade";
        if (args.size() == 2)
            options.version = args.at(1);
        options.force = parser.isSet(forceOption);
        options.dryRun = parser.isSet(dryRunOption);
        std::optional<int> result;
        upgrade.run(options, [&result](int code) {
            result = code;
            QCoreApplication::exit(code);
        });
        return result ? *result : app.exec();
    }

    // Made-up data has nowhere to put real events.
    if (command == QLatin1String("import") && parser.isSet(sampleOption)) {
        err << QObject::tr("callie: import does not work with --sample") << "\n";
        return 2;
    }
    if (command == QLatin1String("import")) {
        LocalCalendars local(LocalCalendars::defaultRoot());
        const QString calendar = parser.value(u"calendar"_s);
        if (calendar.isEmpty())
            return cli::Commands::importFile(local, local, args.value(1), calendar, out, err);
        // A calendar named can be one of an account's.
        int code = 0;
        const std::unique_ptr<Backend> backend = openBackend(false, true, code);
        if (!backend)
            return code;
        code =
            cli::Commands::importFile(*backend->source, local, args.value(1), calendar, out, err);
        sendServerChanges(*backend);
        return code;
    }
    if (command == u"subscribe" || command == u"unsubscribe")
        return runSubscription(command, args.value(1), args.mid(2).join(u' '));
    if (command == QLatin1String("calendars") &&
        (args.value(1) == u"new" || args.value(1) == u"remove"))
        return runCalendarChange(args.value(1), args.mid(2).join(u' '), parser.value(u"account"_s),
                                 parser.isSet(u"yes"_s));
    if (command == QLatin1String("calendars"))
        return args.size() == 1 ? runCalendars(app)
                                : runEvents(app, command, args, parser, parser.isSet(sampleOption));

    if (command == QLatin1String("gui"))
        return QProcess::execute(QStringLiteral("callie-gui"), {});

    if (command == QLatin1String("daemon")) {
        err << QStringLiteral("callie: '%1' is not implemented yet.\n").arg(command);
        return 2;
    }

    err << QStringLiteral("callie: unknown command '%1'\n").arg(command);
    parser.showHelp(1);
}
