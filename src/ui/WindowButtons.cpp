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

WindowButtons::WindowButtons(QObject *parent) : QObject(parent), m_layout{{}, kKnown}
{
    if (!qEnvironmentVariable("XDG_CURRENT_DESKTOP")
             .contains(QLatin1String("KDE"), Qt::CaseInsensitive)) {
        readGnome();
        return;
    }
    const QString path =
        QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("kwinrc"));
    QSettings kwin(path, QSettings::IniFormat);
    kwin.beginGroup(QStringLiteral("org.kde.kdecoration2"));
    // KDE's own defaults apply when the user never changed them.
    apply(fromKde(kwin.value(QStringLiteral("ButtonsOnLeft"), QStringLiteral("MS")).toString(),
                  kwin.value(QStringLiteral("ButtonsOnRight"), QStringLiteral("HIAX")).toString()));
}

WindowButtons *WindowButtons::create(QQmlEngine *engine, QJSEngine *)
{
    return new WindowButtons(engine);
}

void WindowButtons::readGnome()
{
    auto *gsettings = new QProcess(this);
    connect(gsettings, &QProcess::finished, this, [this, gsettings](int code) {
        gsettings->deleteLater();
        if (code != 0)
            return;
        const Layout layout = fromGnome(QString::fromUtf8(gsettings->readAllStandardOutput()));
        if (!layout.left.isEmpty() || !layout.right.isEmpty())
            apply(layout);
    });
    // Without gsettings the default layout stands.
    connect(gsettings, &QProcess::errorOccurred, gsettings, &QObject::deleteLater);
    gsettings->start(QStringLiteral("gsettings"),
                     {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.wm.preferences"),
                      QStringLiteral("button-layout")});
}

void WindowButtons::apply(const Layout &layout)
{
    qCInfo(lcUi) << "window buttons:" << layout.left << layout.right;
    if (layout.left == m_layout.left && layout.right == m_layout.right)
        return;
    m_layout = layout;
    Q_EMIT changed();
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

} // namespace callie
