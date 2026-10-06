#include "WindowButtons.h"

#include "callie/Logging.h"

#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

namespace callie {

namespace {

const QStringList kKnown{QStringLiteral("minimize"), QStringLiteral("maximize"),
                         QStringLiteral("close")};

QStringList gnomeSide(const QString &side)
{
    QStringList buttons;
    for (const QString &name : side.split(u',', Qt::SkipEmptyParts)) {
        const QString button = name.trimmed();
        if (kKnown.contains(button))
            buttons.append(button);
    }
    return buttons;
}

QStringList kdeSide(const QString &letters)
{
    QStringList buttons;
    for (const QChar letter : letters) {
        if (letter == u'I')
            buttons.append(QStringLiteral("minimize"));
        else if (letter == u'A')
            buttons.append(QStringLiteral("maximize"));
        else if (letter == u'X')
            buttons.append(QStringLiteral("close"));
    }
    return buttons;
}

} // namespace

WindowButtons::WindowButtons(Layout layout, QObject *parent)
    : QObject(parent), m_layout(std::move(layout))
{}

WindowButtons *WindowButtons::create(QQmlEngine *engine, QJSEngine *)
{
    return new WindowButtons(detect(), engine);
}

WindowButtons::Layout WindowButtons::fromGnome(const QString &setting)
{
    QString value = setting.trimmed();
    // gsettings prints the string quoted.
    if (value.size() >= 2 && value.startsWith(u'\'') && value.endsWith(u'\''))
        value = value.mid(1, value.size() - 2);
    const qsizetype colon = value.indexOf(u':');
    if (colon < 0)
        return {{}, gnomeSide(value)};
    return {gnomeSide(value.left(colon)), gnomeSide(value.mid(colon + 1))};
}

WindowButtons::Layout WindowButtons::fromKde(const QString &left, const QString &right)
{
    return {kdeSide(left), kdeSide(right)};
}

WindowButtons::Layout WindowButtons::detect()
{
    const Layout fallback{{}, kKnown};
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");

    if (desktop.contains(QLatin1String("KDE"), Qt::CaseInsensitive)) {
        const QString path =
            QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("kwinrc"));
        QSettings kwin(path, QSettings::IniFormat);
        kwin.beginGroup(QStringLiteral("org.kde.kdecoration2"));
        // KDE's own defaults apply when the user never changed them.
        const Layout layout = fromKde(
            kwin.value(QStringLiteral("ButtonsOnLeft"), QStringLiteral("MS")).toString(),
            kwin.value(QStringLiteral("ButtonsOnRight"), QStringLiteral("HIAX")).toString());
        qCInfo(lcUi) << "window buttons from kwinrc:" << layout.left << layout.right;
        return layout;
    }

    QProcess gsettings;
    gsettings.start(QStringLiteral("gsettings"),
                    {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.wm.preferences"),
                     QStringLiteral("button-layout")});
    if (gsettings.waitForFinished(1000) && gsettings.exitCode() == 0) {
        const Layout layout = fromGnome(QString::fromUtf8(gsettings.readAllStandardOutput()));
        qCInfo(lcUi) << "window buttons from gsettings:" << layout.left << layout.right;
        if (!layout.left.isEmpty() || !layout.right.isEmpty())
            return layout;
    }
    return fallback;
}

} // namespace callie
