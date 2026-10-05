#include "ThemeController.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTextStream>

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
    parser.process(app);

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

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Callie.Ui", "Main");

    return app.exec();
}
