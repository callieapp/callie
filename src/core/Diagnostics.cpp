#include "callie/Diagnostics.h"

#include "callie/AccountStore.h"
#include "callie/GoogleCache.h"
#include "callie/GoogleClientConfig.h"
#include "callie/LogFile.h"
#include "callie/TokenStore.h"

#include <kcalendarcore_version.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QSysInfo>
#include <QTimeZone>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>
#include <memory>

namespace callie::diagnostics {

namespace {

constexpr int kLogLines = 30;
// GitHub rejects longer new-issue URLs, and a log tail is the part to trim.
constexpr qsizetype kMaxUrlLength = 8000;

QString lastLines(const QString &path, int count)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QStringList lines = QString::fromUtf8(file.readAll()).split(u'\n', Qt::SkipEmptyParts);
    if (lines.size() > count)
        lines = lines.mid(lines.size() - count);
    return lines.join(u'\n');
}

QString describe(const SyncState &state)
{
    QString text =
        state.lastSynced.isValid()
            ? QStringLiteral("last synced %1").arg(state.lastSynced.toUTC().toString(Qt::ISODate))
            : QStringLiteral("never synced");
    if (!state.lastError.isEmpty())
        text += QStringLiteral(", last error: ") + state.lastError;
    return text;
}

QString environment(const QString &extra)
{
    QStringList lines{
        QStringLiteral("Callie %1").arg(QCoreApplication::applicationVersion()),
        QStringLiteral("Qt %1, KCalendarCore %2")
            .arg(QString::fromLatin1(qVersion()), QStringLiteral(KCALENDARCORE_VERSION_STRING)),
        QStringLiteral("OS: %1, kernel %2, %3")
            .arg(QSysInfo::prettyProductName(), QSysInfo::kernelVersion(),
                 QSysInfo::currentCpuArchitecture()),
        QStringLiteral("Session: %1, desktop %2")
            .arg(qEnvironmentVariable("XDG_SESSION_TYPE", QStringLiteral("unknown")),
                 qEnvironmentVariable("XDG_CURRENT_DESKTOP", QStringLiteral("unknown"))),
        QStringLiteral("Locale: %1, time zone %2")
            .arg(QLocale().name(), QString::fromUtf8(QTimeZone::systemTimeZoneId())),
        QStringLiteral("Google OAuth client: %1")
            .arg(GoogleClientConfig::resolve().isValid() ? QStringLiteral("configured")
                                                         : QStringLiteral("not configured")),
    };
    if (!extra.isEmpty())
        lines.append(extra);
    return lines.join(u'\n');
}

QString cacheSummary(GoogleCache &cache, const QList<Account> &accounts,
                     const QHash<QString, QString> &tokenState)
{
    QStringList lines;
    for (const Account &account : accounts) {
        lines.append(QStringLiteral("Account %1 (%2): token %3, %4")
                         .arg(account.id, account.provider, tokenState.value(account.id),
                              describe(cache.accountState(account))));
        const QList<GoogleCalendar> calendars = cache.calendars(account);
        int shown = 0;
        for (const GoogleCalendar &calendar : calendars) {
            shown += calendar.selected ? 1 : 0;
            const SyncState state = cache.calendarState(account, calendar.id);
            if (!state.lastError.isEmpty() || !state.lastSynced.isValid())
                lines.append(QStringLiteral("  calendar %1: %2").arg(calendar.id, describe(state)));
        }
        lines.append(QStringLiteral("  %1 calendars, %2 shown, %3 events cached")
                         .arg(calendars.size())
                         .arg(shown)
                         .arg(cache.eventCount(account)));
    }
    return lines.join(u'\n');
}

} // namespace

QString redact(const QString &text)
{
    static const QRegularExpression email(
        QStringLiteral(R"([A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,})"));
    QHash<QString, int> seen;
    QString result;
    qsizetype from = 0;
    for (const QRegularExpressionMatch &match : email.globalMatch(text)) {
        result += text.mid(from, match.capturedStart() - from);
        const QString address = match.captured().toLower();
        if (!seen.contains(address))
            seen.insert(address, int(seen.size()) + 1);
        result += QStringLiteral("<email-%1>").arg(seen.value(address));
        from = match.capturedEnd();
    }
    result += text.mid(from);
    // The home folder's name is usually the user's name.
    const QString home = QDir::homePath();
    if (home.size() > 1)
        result.replace(home, QStringLiteral("~"));
    return result;
}

void collect(TokenStore &tokens, const QString &extra, std::function<void(QString)> done)
{
    QList<Account> accounts;
    AccountStore store(AccountStore::defaultPath());
    const QString accountsProblem = store.load(accounts) ? QString() : store.errorString();

    // Keyring reads finish one by one; the report is written once all are back.
    auto tokenState = std::make_shared<QHash<QString, QString>>();
    auto remaining = std::make_shared<qsizetype>(accounts.size());
    auto finish = [accounts, accountsProblem, extra, tokenState, done = std::move(done)] {
        QStringList sections{environment(extra)};

        GoogleCache cache(GoogleCache::defaultPath());
        const QFileInfo cacheFile(GoogleCache::defaultPath());
        QStringList state{
            QStringLiteral("Cache: %1 (%2 KB)")
                .arg(cacheFile.exists() ? QStringLiteral("present") : QStringLiteral("missing"))
                .arg(cacheFile.size() / 1024),
            QStringLiteral("Logs: %1").arg(logfile::defaultDirectory()),
        };
        if (!accountsProblem.isEmpty())
            state.append(QStringLiteral("Accounts file: %1").arg(accountsProblem));
        else if (accounts.isEmpty())
            state.append(QStringLiteral("Accounts: none"));
        else if (!cache.open())
            state.append(QStringLiteral("Cache error: %1").arg(cache.errorString()));
        else
            state.append(cacheSummary(cache, accounts, *tokenState));
        sections.append(state.join(u'\n'));

        const QDir logs(logfile::defaultDirectory());
        for (const QString &name :
             {QStringLiteral("callie-gui.log"), QStringLiteral("callie.log")}) {
            const QString tail = lastLines(logs.filePath(name), kLogLines);
            if (!tail.isEmpty())
                sections.append(
                    QStringLiteral("Last %1 lines of %2:\n%3").arg(kLogLines).arg(name, tail));
        }
        done(redact(sections.join(QStringLiteral("\n\n"))));
    };

    if (accounts.isEmpty()) {
        // Still asynchronous, so callers see one behavior.
        QTimer::singleShot(0, finish);
        return;
    }
    for (const Account &account : std::as_const(accounts)) {
        tokens.read(account, [account, tokenState, remaining, finish](const QString &secret,
                                                                      const QString &error) {
            tokenState->insert(account.id, !error.isEmpty()
                                               ? QStringLiteral("unreadable (%1)").arg(error)
                                           : secret.isEmpty() ? QStringLiteral("missing")
                                                              : QStringLiteral("stored"));
            if (--*remaining == 0)
                finish();
        });
    }
}

QUrl issueUrl(const QString &report)
{
    const auto build = [](const QString &body) {
        QUrl url(QStringLiteral("https://github.com/callieapp/callie/issues/new"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("template"), QStringLiteral("bug_report.yml"));
        query.addQueryItem(QStringLiteral("diagnostics"), body);
        url.setQuery(query);
        return url;
    };
    const QString note = QStringLiteral("\n[shortened; run `callie doctor` for the rest]");

    // The limit is on the encoded URL, where a space or newline takes three
    // characters, so the report is cut until the encoded form fits.
    QUrl url = build(report);
    QString body = report;
    while (url.toString(QUrl::FullyEncoded).size() > kMaxUrlLength && !body.isEmpty()) {
        const qsizetype over = url.toString(QUrl::FullyEncoded).size() - kMaxUrlLength;
        body.chop(std::max<qsizetype>(over / 3, 64));
        url = build(body + note);
    }
    return url;
}

} // namespace callie::diagnostics
