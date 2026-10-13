#include "AccountsController.h"
#include "Clock.h"
#include "EventModelForeign.h"
#include "ImportController.h"
#include "LiveQml.h"
#include "LocalController.h"
#include "NotificationServer.h"
#include "PlaceSearch.h"
#include "ReleaseInfo.h"
#include "Reminders.h"
#include "SingleInstance.h"
#include "StartAtLogin.h"
#include "SubscriptionsController.h"
#include "ThemeController.h"
#include "ThemesController.h"
#include "TrayItem.h"

#include "callie/AccountStore.h"
#include "callie/Autostart.h"
#include "callie/CalDavSource.h"
#include "callie/CombinedSource.h"
#include "callie/GoogleCache.h"
#include "callie/GoogleCalendarApi.h"
#include "callie/GoogleSource.h"
#include "callie/GoogleSync.h"
#include "callie/GoogleTokenProvider.h"
#include "callie/Identity.h"
#include "callie/LocalCalendars.h"
#include "callie/LogFile.h"
#include "callie/Logging.h"
#include "callie/LookedSource.h"
#include "callie/QueuedSource.h"
#include "callie/ReminderScheduler.h"
#include "callie/SampleSource.h"
#include "callie/Subscriptions.h"
#include "callie/TokenStore.h"
#include "callie/UpdateCheck.h"

#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QIcon>
#include <QNetworkAccessManager>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <optional>

int main(int argc, char *argv[])
{
    QElapsedTimer started;
    started.start();
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Callie"));
    app.setOrganizationName(QStringLiteral("Callie"));
    app.setApplicationVersion(QStringLiteral(CALLIE_VERSION));
    // Lets Wayland associate the window with the .desktop entry.
    app.setDesktopFileName(callie::identity::appId());
    // Shown by desktops that read the window's own icon, such as KDE, even when
    // no desktop entry is installed; the built-in logo stands in for the theme's.
    app.setWindowIcon(QIcon::fromTheme(callie::identity::appId(),
                                       QIcon(QStringLiteral(":/callie/assets/logo.png"))));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption themeOption(
        QStringLiteral("theme"),
        QStringLiteral("Built-in theme id or path to a theme file. Defaults to $CALLIE_THEME."),
        QStringLiteral("theme"), qEnvironmentVariable("CALLIE_THEME"));
    parser.addOption(themeOption);
    QCommandLineOption sampleOption(
        QStringLiteral("sample"), QStringLiteral("Show a made-up week instead of your calendars."));
    parser.addOption(sampleOption);
    QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"),
        QStringLiteral("Save the window to a PNG once it has rendered, then exit."),
        QStringLiteral("file"));
    parser.addOption(screenshotOption);
    QCommandLineOption nowOption(
        QStringLiteral("now"),
        QStringLiteral("Pretend it is this local time, e.g. 2026-03-18T10:40, and stop the clock."),
        QStringLiteral("time"));
    parser.addOption(nowOption);
    QCommandLineOption sizeOption(QStringLiteral("size"),
                                  QStringLiteral("Window size in pixels, e.g. 1280x840."),
                                  QStringLiteral("size"));
    parser.addOption(sizeOption);
    QCommandLineOption viewOption(QStringLiteral("view"),
                                  QStringLiteral("With --screenshot: show this view, day, week, "
                                                 "month or agenda."),
                                  QStringLiteral("view"));
    parser.addOption(viewOption);
    QCommandLineOption backgroundOption(
        QString::fromLatin1(callie::Autostart::kBackgroundOption),
        QStringLiteral("Start without showing the window, as when started at login."));
    parser.addOption(backgroundOption);
#ifdef CALLIE_LIVE_QML
    QCommandLineOption liveOption(QStringLiteral("live-qml"),
                                  QStringLiteral("Load QML from the source tree and reload it on "
                                                 "save. Development builds only."));
    parser.addOption(liveOption);
#endif
    parser.addPositionalArgument(QStringLiteral("files"),
                                 QStringLiteral("Calendar files (.ics) to add events from."),
                                 QStringLiteral("[file.ics...]"));
    parser.process(app);
    if (parser.isSet(nowOption)) {
        const QDateTime now = QDateTime::fromString(parser.value(nowOption), Qt::ISODate);
        if (!now.isValid()) {
            QTextStream(stderr) << "callie-gui: --now wants a time like 2026-03-18T10:40\n";
            return 2;
        }
        callie::Clock::instance()->freeze(now);
    }
    QSize windowSize;
    if (parser.isSet(sizeOption)) {
        const QStringList parts = parser.value(sizeOption).split(u'x');
        windowSize = QSize(parts.value(0).toInt(), parts.value(1).toInt());
        if (parts.size() != 2 || windowSize.isEmpty()) {
            QTextStream(stderr) << "callie-gui: --size wants a size like 1280x840\n";
            return 2;
        }
    }
    // Only for screenshots, whose settings are thrown away, so the view the
    // user last chose is kept.
    if (parser.isSet(viewOption)) {
        if (!parser.isSet(screenshotOption)) {
            QTextStream(stderr) << "callie-gui: --view goes with --screenshot\n";
            return 2;
        }
        if (!callie::Settings::views().contains(parser.value(viewOption))) {
            QTextStream(stderr) << "callie-gui: --view wants one of "
                                << callie::Settings::views().join(u", ") << "\n";
            return 2;
        }
    }
    callie::logfile::install(QStringLiteral("callie-gui"));

    // Sample data and screenshots run beside a real Callie; anything else
    // hands over to the one already running.
    const bool useSample = parser.isSet(sampleOption);
    const QString screenshot = parser.value(screenshotOption);
    const bool standalone = useSample || !screenshot.isEmpty();
    callie::SingleInstance single(callie::identity::appId(), QDBusConnection::sessionBus());
    // As a file manager passes them: paths, or file:// URLs.
    QStringList opened;
    for (const QString &argument : parser.positionalArguments())
        opened
            << QUrl::fromUserInput(argument, QDir::currentPath(), QUrl::AssumeLocalFile).toString();
    if (!standalone && !single.claim(opened))
        return 0;

    // Screenshots show the defaults, whatever this user has chosen.
    QTemporaryDir scratch;
    callie::Settings settings(parser.isSet(screenshotOption)
                                  ? scratch.filePath(QStringLiteral("settings.ini"))
                                  : callie::Settings::defaultPath());
    if (parser.isSet(viewOption))
        settings.setView(parser.value(viewOption));
    callie::SettingsForeign::s_instance = &settings;
    // Debug lines on request, from the developer settings.
    callie::logfile::setDebug(settings.verboseLogging());
    QObject::connect(&settings, &callie::Settings::developerChanged, &settings,
                     [&settings] { callie::logfile::setDebug(settings.verboseLogging()); });

    QTextStream err(stderr);
    if (const QString theme = parser.value(themeOption); !theme.isEmpty()) {
        const QStringList errors = callie::ThemesController::instance()->applyForThisRun(theme);
        for (const QString &error : errors)
            err << "callie-gui: " << theme << ": " << error << "\n";
    } else {
        callie::ThemesController::instance()->restore();
    }
    for (const QString &warning : callie::ThemeController::instance()->warnings())
        err << "callie-gui: theme: " << warning << "\n";
    err.flush();

    // Basic style: everything visual comes from the theme, not a platform style.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Everything the source reads from or syncs with lives as long as the app.
    QList<callie::Account> accounts;
    callie::AccountStore store(callie::AccountStore::defaultPath());
    const bool accountsRead = store.load(accounts);
    if (!accountsRead)
        err << "callie-gui: " << store.errorString() << "\n";
    callie::GoogleCache cache(callie::GoogleCache::defaultPath());
    // Sample data never touches the real cache. Without accounts it is still
    // opened, since one can be connected from settings.
    if (!useSample && !cache.open())
        err << "callie-gui: " << cache.errorString() << "\n";
    err.flush();
    QList<callie::CalDavSource::Account> calDavAccounts;
    for (const callie::Account &account : std::as_const(accounts)) {
        if (account.provider == u"caldav")
            calDavAccounts << callie::CalDavSource::Account{account.id, QUrl(account.server),
                                                            account.user};
    }
    accounts.removeIf([](const callie::Account &a) { return a.provider != u"google"; });

    callie::KeychainTokenStore tokens;
    const callie::GoogleClientConfig client = callie::GoogleClientConfig::resolve();
    callie::GoogleTokenProvider provider(client, tokens);
    QNetworkAccessManager network;
    callie::GoogleCalendarApi api(&network);
    callie::GoogleSync sync(provider, api, cache);
    callie::GoogleSource google(cache, accounts);
    google.setSync(&sync);
    QObject::connect(&sync, &callie::GoogleSync::photoFound, &settings,
                     [&settings](const callie::Account &account, const QUrl &photo) {
                         settings.setAccountPhoto(account.id, photo);
                     });
    // A newly connected account brings its Google week and clock choices along.
    QObject::connect(&sync, &callie::GoogleSync::settingsFound, &settings,
                     [&settings](const callie::Account &, const QHash<QString, QString> &google) {
                         settings.seedFromGoogle(google);
                     });
    // Every account and calendar, shown together. Sample data and screenshots
    // leave the calendars on this computer alone.
    std::optional<callie::LocalCalendars> local;
    std::optional<callie::Subscriptions> subscriptions;
    std::optional<callie::CalDavSource> caldav;
    if (!standalone) {
        local.emplace(callie::LocalCalendars::defaultRoot());
        subscriptions.emplace(callie::Subscriptions::defaultRoot(), &network);
        caldav.emplace(callie::CalDavSource::defaultRoot(), &network, &tokens);
        caldav->setAccounts(calDavAccounts);
        caldav->followLooks(&settings);
    }
    callie::CombinedSource combined;
    combined.setSources(
        local ? QList<callie::CalendarSource *>{&google, &*caldav, &*local, &*subscriptions}
              : QList<callie::CalendarSource *>{&google});
    callie::SampleSource sample;
    sample.setNow([] { return callie::Clock::instance()->now(); });

    // Changes go through a queue that shows them at once, holds them a moment
    // so they can be undone, and sends them when it can. Sample data gets one
    // of its own that is never saved; screenshots change nothing, so need none.
    std::optional<callie::QueuedSource> queued;
    if (useSample)
        queued.emplace(sample, QString());
    else if (!standalone)
        queued.emplace(combined, callie::QueuedSource::defaultPath());
    callie::CalendarSource *backend =
        queued ? static_cast<callie::CalendarSource *>(&*queued) : &combined;
    // Everything reads through the user's own names and colors for calendars.
    callie::LookedSource looked(*backend, settings);
    callie::CalendarSource *source = &looked;
    // A calendar Callie has not seen starts hidden if Google does not show it;
    // after that, Callie's own choice holds.
    const auto seedCalendars = [&settings, source] { settings.seedCalendars(source->calendars()); };
    QObject::connect(source, &callie::CalendarSource::changed, &settings, seedCalendars);
    // Reset to defaults forgets which calendars were seen, so they start over.
    QObject::connect(&settings, &callie::Settings::hiddenCalendarsChanged, &settings,
                     seedCalendars);
    seedCalendars();
    // People to suggest as guests: from the events, and from Google's contacts.
    callie::ContactBook contacts(standalone ? QString() : callie::ContactBook::defaultPath());
    contacts.setShown([&settings](const callie::CalendarInfo &c) { return settings.isShown(c); });
    QObject::connect(&settings, &callie::Settings::hiddenCalendarsChanged, &contacts,
                     &callie::ContactBook::reread);
    // Accounts removed while Callie was closed take their contacts, but an
    // account list that cannot be read says nothing about which are gone.
    if (accountsRead) {
        QStringList ids;
        for (const callie::Account &account : std::as_const(accounts))
            ids << account.id;
        contacts.keepOnly(ids);
    }
    QObject::connect(
        &sync, &callie::GoogleSync::contactsFound, &contacts,
        [&contacts](const callie::Account &account, const QList<callie::Contact> &people) {
            contacts.setContacts(account.id, people);
        });
    callie::ContactBookForeign::s_instance = &contacts;
    callie::PlaceSearch places;
    places.setSource(source);
    const auto placesOnline = [&] {
        places.setServer(!standalone && settings.searchPlacesOnline()
                             ? callie::PlaceSearch::publicServer()
                             : QUrl());
    };
    placesOnline();
    QObject::connect(&settings, &callie::Settings::placesChanged, &places, placesOnline);
    callie::PlaceSearchForeign::s_instance = &places;
    QTimer syncTimer;
    // Without a client every refresh would fail, so show the cache and say why once.
    const bool canSync = client.isValid();
    if (!useSample && !accounts.isEmpty() && !canSync)
        qCWarning(lcSync) << "no Google OAuth client is configured, showing cached events only."
                          << "Build with one, or set CALLIE_GOOGLE_CLIENT_ID and"
                          << "CALLIE_GOOGLE_CLIENT_SECRET.";
    if (!useSample && canSync && screenshot.isEmpty()) {
        QObject::connect(&syncTimer, &QTimer::timeout, &google, &callie::GoogleSource::refresh);
        syncTimer.start(std::chrono::minutes(5));
        if (!accounts.isEmpty())
            QTimer::singleShot(0, &google, &callie::GoogleSource::refresh);
    }
    // CalDAV accounts sync as often as Google's, whatever the Google sign-in.
    QTimer calDavTimer;
    if (caldav) {
        QObject::connect(&calDavTimer, &QTimer::timeout, &*caldav, &callie::CalDavSource::refresh);
        calDavTimer.start(std::chrono::minutes(5));
        QTimer::singleShot(0, &*caldav, &callie::CalDavSource::refresh);
    }
    // Subscriptions look every quarter hour for feeds due again, which is hourly.
    QTimer feedTimer;
    if (subscriptions) {
        QObject::connect(&feedTimer, &QTimer::timeout, &*subscriptions,
                         &callie::Subscriptions::refresh);
        feedTimer.start(std::chrono::minutes(15));
        QTimer::singleShot(0, &*subscriptions, &callie::Subscriptions::refresh);
    }

    // Screenshots and sample data stay quiet.
    callie::ReminderScheduler scheduler;
    callie::FreedesktopNotifications notifications;
    if (!standalone) {
        scheduler.setSource(source);
        callie::Reminders::instance()->setup(&scheduler, &notifications, &settings);
        // Closing the window can leave Callie running so reminders still come.
        app.setQuitOnLastWindowClosed(!settings.keepRunning());
        QObject::connect(&settings, &callie::Settings::keepRunningChanged, &app,
                         [&] { app.setQuitOnLastWindowClosed(!settings.keepRunning()); });
    }
    callie::Autostart autostart(callie::identity::appId(), QCoreApplication::applicationFilePath());
    if (!standalone)
        callie::StartAtLogin::instance()->setup(&autostart, &settings);

    callie::AccountsController::Setup accountSetup{&store,   &tokens, &cache, &google,
                                                   &network, client,  {}};
    accountSetup.contacts = &contacts;
    accountSetup.caldav = caldav ? &*caldav : nullptr;
    // Sample data and screenshots never show or touch the user's accounts.
    if (standalone) {
        accountSetup = {};
        accountSetup.unavailable =
            QObject::tr("Showing sample data, so accounts cannot be changed here.");
    }
    callie::AccountsController::instance()->setUp(accountSetup);
    callie::LocalController::instance()->setUp(local ? &*local : nullptr);
    callie::ImportController::instance()->setUp(local ? &looked : nullptr,
                                                local ? &*local : nullptr);
    callie::SubscriptionsController::instance()->setUp(subscriptions ? &*subscriptions : nullptr);

    callie::UpdateCheck updates;
    QQmlApplicationEngine engine;
    // Started at login, Callie waits hidden until it is opened or a reminder is
    // clicked; that only makes sense while it is set to keep running.
    const bool hidden = parser.isSet(backgroundOption) && settings.keepRunning() && !standalone;
    engine.setInitialProperties({{QStringLiteral("source"), QVariant::fromValue(source)},
                                 {QStringLiteral("visible"), !hidden}});
    bool live = false;
#ifdef CALLIE_LIVE_QML
    // A QML mistake while editing should wait for the fix, not end the session.
    callie::LiveQml liveQml(
        engine,
        {{QStringLiteral("Callie.Ui"), QStringLiteral(CALLIE_SOURCE_DIR "/src/ui"),
          QStringLiteral(CALLIE_BINARY_DIR "/Callie/Ui")}},
        [&engine] { engine.loadFromModule("Callie.Ui", "Main"); });
    if (parser.isSet(liveOption)) {
        live = liveQml.start();
        if (!live)
            err << "callie-gui: live QML: " << liveQml.errorString() << "\n" << Qt::flush;
    }
#endif
    if (!live)
        QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
            [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Callie.Ui", "Main");

    // Guests are suggested from two years of events, a read that waits until
    // the window has drawn so it does not hold up the window's own.
    auto *window = engine.rootObjects().isEmpty()
                       ? nullptr
                       : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (window && window->isVisible()) {
        QObject::connect(
            window, &QQuickWindow::frameSwapped, &contacts,
            [&contacts, source, &started] {
                qCInfo(lcUi) << "window shown after" << started.elapsed() << "ms";
                contacts.setSource(source);
            },
            static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection));
    } else {
        contacts.setSource(source);
    }

    // Sample data and screenshots never ask GitHub about releases.
    if (!standalone) {
        if (auto *release = engine.singletonInstance<callie::ReleaseInfo *>("Callie.Ui", "Release"))
            release->watchForUpdates(&settings, &updates);
    }

    // A second launch, a reminder or the tray brings back a hidden window.
    const auto showWindow = [&engine] {
        if (engine.rootObjects().isEmpty())
            return;
        // Looked up each time: live QML replaces the window.
        if (auto *shown = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
            shown->show();
            shown->raise();
            shown->requestActivate();
        }
    };
    QObject::connect(&single, &callie::SingleInstance::activated, &engine, showWindow);
    // Files opened from the file manager, now or by a later launch.
    const auto openFiles = [&engine](const QStringList &uris) {
        if (engine.rootObjects().isEmpty() || uris.isEmpty())
            return;
        QVariantList urls;
        for (const QString &uri : uris)
            urls << QUrl(uri);
        QMetaObject::invokeMethod(engine.rootObjects().first(), "openFiles",
                                  Q_ARG(QVariant, QVariant(urls)));
    };
    QObject::connect(&single, &callie::SingleInstance::opened, &engine, openFiles);
    QTimer::singleShot(0, &engine, [openFiles, opened] { openFiles(opened); });

    // While Callie keeps running without its window, the tray shows it is there.
    callie::TrayItem tray(QDBusConnection::sessionBus());
    if (!standalone) {
        tray.setVisible(settings.keepRunning());
        QObject::connect(&settings, &callie::Settings::keepRunningChanged, &tray,
                         [&] { tray.setVisible(settings.keepRunning()); });
        QObject::connect(&tray, &callie::TrayItem::openRequested, &engine, showWindow);
        QObject::connect(&tray, &callie::TrayItem::syncRequested, source,
                         &callie::CalendarSource::refresh);
        QObject::connect(&tray, &callie::TrayItem::quitRequested, &app, &QCoreApplication::quit);
    }

    if (windowSize.isValid() && window)
        window->resize(windowSize);

    if (!screenshot.isEmpty()) {
        if (!window)
            return 1;
        QTimer::singleShot(1000, window, [window, screenshot] {
            QCoreApplication::exit(window->grabWindow().save(screenshot) ? 0 : 1);
        });
    }

    return app.exec();
}
