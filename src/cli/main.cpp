#include "callie/AccountManager.h"
#include "callie/AccountStore.h"
#include "callie/EventModel.h"
#include "callie/GoogleAuth.h"
#include "callie/GoogleCalendarApi.h"
#include "callie/SampleSource.h"
#include "callie/TokenStore.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDate>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QTextStream>
#include <QTimer>

#include <algorithm>

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

int runAgenda(int days)
{
    SampleSource source;
    const QTimeZone tz = QTimeZone::systemTimeZone();
    const QDate today = QDate::currentDate();
    const QDateTime from(today, QTime(0, 0), tz);
    const QDateTime to(today.addDays(days), QTime(0, 0), tz);

    QList<Event> events = source.eventsBetween(from, to, tz);
    std::sort(events.begin(), events.end(),
              [](const Event &a, const Event &b) { return a.start < b.start; });

    if (events.isEmpty()) {
        out << dim(QObject::tr("Nothing scheduled.")) << "\n";
        return 0;
    }

    QDate current;
    for (const Event &e : std::as_const(events)) {
        if (e.start.date() != current) {
            current = e.start.date();
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
    const QList<Account> accounts = AccountStore(AccountStore::defaultPath()).accounts();
    if (accounts.isEmpty()) {
        err << QObject::tr("No accounts. Add one with: callie accounts add google") << "\n";
        return 0;
    }
    for (const Account &account : accounts)
        out << account.provider << "\t" << account.id << "\n";
    return 0;
}

int runAccountsAddGoogle(QCoreApplication &app)
{
    const GoogleClientConfig client = GoogleClientConfig::resolve();
    if (!client.isValid()) {
        err << QObject::tr("callie: no Google OAuth client is configured. Build with one, or set "
                           "CALLIE_GOOGLE_CLIENT_ID and CALLIE_GOOGLE_CLIENT_SECRET.")
            << "\n";
        return 1;
    }

    GoogleAuth auth(client);
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

    QTimer::singleShot(std::chrono::minutes(5), [] {
        err << QObject::tr("callie: timed out waiting for Google sign-in") << "\n";
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
    AccountManager manager(tokens, store);

    QObject::connect(&manager, &AccountManager::removed, [] { finish(0); });
    QObject::connect(&manager, &AccountManager::failed, [](const QString &message) {
        err << QStringLiteral("callie: %1\n").arg(message);
        finish(1);
    });

    QTimer::singleShot(0, &manager, [&] { manager.remove(account); });
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
                       "  agenda    Upcoming events (default)\n"
                       "  accounts  List, add or remove calendar accounts\n"
                       "  add       Create an event from natural language\n"
                       "  sync      Refresh all accounts now\n"
                       "  daemon    Run background sync and notifications\n"
                       "  gui       Launch the desktop app"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("Command to run."));

    QCommandLineOption daysOption({QStringLiteral("d"), QStringLiteral("days")},
                                  QStringLiteral("Days of agenda to show."), QStringLiteral("n"),
                                  QStringLiteral("7"));
    parser.addOption(daysOption);
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    const QString command = args.isEmpty() ? QStringLiteral("agenda") : args.first();

    if (command == QLatin1String("agenda"))
        return runAgenda(parser.value(daysOption).toInt());

    if (command == QLatin1String("accounts"))
        return runAccounts(app, args);

    if (command == QLatin1String("gui"))
        return QProcess::execute(QStringLiteral("callie-gui"), {});

    if (command == QLatin1String("add") || command == QLatin1String("sync") ||
        command == QLatin1String("daemon")) {
        err << QStringLiteral("callie: '%1' is not implemented yet.\n").arg(command);
        return 2;
    }

    err << QStringLiteral("callie: unknown command '%1'\n").arg(command);
    parser.showHelp(1);
}
