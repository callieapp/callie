#include "ThemeController.h"

#include "callie/LogFile.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTextStream>
#include <QTimer>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("callie-gallery"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Renders a Callie theme's tokens and components. Edits to a theme file "
                       "appear live."));
    parser.addHelpOption();
    QCommandLineOption themeOption(QStringLiteral("theme"),
                                   QStringLiteral("Built-in theme id or path to a theme file."),
                                   QStringLiteral("theme"));
    parser.addOption(themeOption);
    QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"),
        QStringLiteral("Save the rendered gallery to a PNG and exit."), QStringLiteral("file"));
    parser.addOption(screenshotOption);
    parser.process(app);
    callie::logfile::install(QStringLiteral("callie-gallery"));

    if (const QString theme = parser.value(themeOption); !theme.isEmpty()) {
        const QStringList errors = callie::ThemeController::instance()->load(theme);
        QTextStream err(stderr);
        for (const QString &error : errors)
            err << "callie-gallery: " << theme << ": " << error << "\n";
        if (!errors.isEmpty())
            return 1;
    }

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Callie.Gallery", "Gallery");

    if (const QString file = parser.value(screenshotOption); !file.isEmpty()) {
        auto *window = engine.rootObjects().isEmpty()
                           ? nullptr
                           : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        if (!window)
            return 1;
        // Once layout has settled, grow the window to fit the whole page, then
        // give it a moment to render at that size before grabbing.
        QTimer::singleShot(500, window, [window, file] {
            if (auto *content = window->findChild<QObject *>(QStringLiteral("content")))
                window->setHeight(content->property("implicitHeight").toInt());
            QTimer::singleShot(500, window, [window, file] {
                QCoreApplication::exit(window->grabWindow().save(file) ? 0 : 1);
            });
        });
    }
    return app.exec();
}
