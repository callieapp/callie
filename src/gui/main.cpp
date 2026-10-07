#include "AccountsController.h"
#include "Clock.h"
#include "EventModelForeign.h"
#include "LiveQml.h"
#include "NotificationServer.h"
#include "Reminders.h"
#include "SingleInstance.h"
#include "ThemeController.h"
#include "ThemesController.h"

#include "callie/AccountStore.h"
#include "callie/GoogleCache.h"
#include "callie/GoogleCalendarApi.h"
#include "callie/GoogleSource.h"
#include "callie/GoogleSync.h"
#include "callie/GoogleTokenProvider.h"
#include "callie/LogFile.h"
#include "callie/Logging.h"
#include "callie/ReminderScheduler.h"
#include "callie/SampleSource.h"
#include "callie/TokenStore.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QNetworkAccessManager>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include <chrono>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Callie"));
    app.setOrganizationName(QStringLiteral("Callie"));
    app.setApplicationVersion(QStringLiteral(CALLIE_VERSION));
    // Lets Wayland associate the window with the .desktop entry.
    app.setDesktopFileName(QStringLiteral(CALLIE_APP_ID));
    // Shown by desktops that read the window's own icon, such as KDE, even when
    // no desktop entry is installed; the built-in logo stands in for the theme's.
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral(CALLIE_APP_ID),
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
#ifdef CALLIE_LIVE_QML
    QCommandLineOption liveOption(QStringLiteral("live-qml"),
                                  QStringLiteral("Load QML from the source tree and reload it on "
                                                 "save. Development builds only."));
    parser.addOption(liveOption);
#endif
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
    callie::logfile::install(QStringLiteral("callie-gui"));

    // Sample data and screenshots run beside a real Callie; anything else
    // hands over to the one already running.
    const bool useSample = parser.isSet(sampleOption);
    const QString screenshot = parser.value(screenshotOption);
    const bool standalone = useSample || !screenshot.isEmpty();
    callie::SingleInstance single(QStringLiteral(CALLIE_APP_ID), QDBusConnection::sessionBus());
    if (!standalone && !single.claim())
        return 0;

    // Screenshots show the defaults, whatever this user has chosen.
    QTemporaryDir scratch;
    callie::Settings settings(parser.isSet(screenshotOption)
                                  ? scratch.filePath(QStringLiteral("settings.ini"))
                                  : callie::Settings::defaultPath());
    callie::SettingsForeign::s_instance = &settings;

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
    if (!store.load(accounts))
        err << "callie-gui: " << store.errorString() << "\n";
    callie::GoogleCache cache(callie::GoogleCache::defaultPath());
    // Sample data never touches the real cache. Without accounts it is still
    // opened, since one can be connected from settings.
    if (!useSample && !cache.open())
        err << "callie-gui: " << cache.errorString() << "\n";
    err.flush();
    accounts.removeIf([](const callie::Account &a) { return a.provider != u"google"; });

    callie::KeychainTokenStore tokens;
    const callie::GoogleClientConfig client = callie::GoogleClientConfig::resolve();
    callie::GoogleTokenProvider provider(client, tokens);
    QNetworkAccessManager network;
    callie::GoogleCalendarApi api(&network);
    callie::GoogleSync sync(provider, api, cache);
    callie::GoogleSource google(cache, accounts);
    google.setSync(&sync);
    // A newly connected account brings its Google week and clock choices along.
    QObject::connect(&sync, &callie::GoogleSync::settingsFound, &settings,
                     [&settings](const callie::Account &, const QHash<QString, QString> &google) {
                         settings.seedFromGoogle(google);
                     });
    callie::SampleSource sample;
    sample.setNow([] { return callie::Clock::instance()->now(); });

    callie::CalendarSource *source =
        useSample ? static_cast<callie::CalendarSource *>(&sample) : &google;
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

    callie::AccountsController::Setup accountSetup{&store,   &tokens, &cache, &google,
                                                   &network, client,  {}};
    // Sample data and screenshots never show or touch the user's accounts.
    if (standalone) {
        accountSetup = {};
        accountSetup.unavailable =
            QObject::tr("Showing sample data, so accounts cannot be changed here.");
    }
    callie::AccountsController::instance()->setUp(accountSetup);

    QQmlApplicationEngine engine;
    engine.setInitialProperties({{QStringLiteral("source"), QVariant::fromValue(source)}});
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

    // A second launch, or a reminder, brings back a hidden window.
    QObject::connect(&single, &callie::SingleInstance::activated, &engine, [&engine] {
        if (engine.rootObjects().isEmpty())
            return;
        if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
            window->show();
            window->raise();
            window->requestActivate();
        }
    });

    if (windowSize.isValid() && !engine.rootObjects().isEmpty()) {
        if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()))
            window->resize(windowSize);
    }

    if (!screenshot.isEmpty()) {
        auto *window = engine.rootObjects().isEmpty()
                           ? nullptr
                           : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        if (!window)
            return 1;
        QTimer::singleShot(1000, window, [window, screenshot] {
            QCoreApplication::exit(window->grabWindow().save(screenshot) ? 0 : 1);
        });
    }

    return app.exec();
}
