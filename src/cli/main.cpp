#include "callie/EventModel.h"
#include "callie/SampleSource.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDate>
#include <QProcess>
#include <QTextStream>

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
