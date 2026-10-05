#include "LiveQml.h"
#include "ThemeController.h"

#include "callie/AccountStore.h"
#include "callie/GoogleCache.h"
#include "callie/GoogleCalendarApi.h"
#include "callie/GoogleSource.h"
#include "callie/GoogleSync.h"
#include "callie/GoogleTokenProvider.h"
#include "callie/LogFile.h"
#include "callie/Logging.h"
#include "callie/SampleSource.h"
#include "callie/TokenStore.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QNetworkAccessManager>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
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
#ifdef CALLIE_LIVE_QML
    QCommandLineOption liveOption(QStringLiteral("live-qml"),
                                  QStringLiteral("Load QML from the source tree and reload it on "
                                                 "save. Development builds only."));
    parser.addOption(liveOption);
#endif
    parser.process(app);
    callie::logfile::install(QStringLiteral("callie-gui"));

    QTextStream err(stderr);
    if (const QString theme = parser.value(themeOption); !theme.isEmpty()) {
        const QStringList errors = callie::ThemeController::instance()->load(theme);
        for (const QString &error : errors)
            err << "callie-gui: " << theme << ": " << error << "\n";
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
    // Sample data never touches the real cache.
    const bool useSample = parser.isSet(sampleOption);
    if (!useSample && !accounts.isEmpty() && !cache.open())
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
    callie::SampleSource sample;

    callie::CalendarSource *source =
        useSample ? static_cast<callie::CalendarSource *>(&sample) : &google;
    QTimer syncTimer;
    const QString screenshot = parser.value(screenshotOption);
    // Without a client every refresh would fail, so show the cache and say why once.
    const bool canSync = client.isValid();
    if (!useSample && !accounts.isEmpty() && !canSync)
        qCWarning(lcSync) << "no Google OAuth client is configured, showing cached events only."
                          << "Build with one, or set CALLIE_GOOGLE_CLIENT_ID and"
                          << "CALLIE_GOOGLE_CLIENT_SECRET.";
    if (!useSample && !accounts.isEmpty() && canSync && screenshot.isEmpty()) {
        QObject::connect(&syncTimer, &QTimer::timeout, &google, &callie::GoogleSource::refresh);
        syncTimer.start(std::chrono::minutes(5));
        QTimer::singleShot(0, &google, &callie::GoogleSource::refresh);
    }

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
