#pragma once

#include "callie/ThemeLibrary.h"

#include <QObject>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantList>

namespace callie {

class Settings;

/// The theme choices the settings dialog offers: built-in and user themes,
/// a color editor for the user's own, and import and export. The choice is
/// remembered in Settings and applied through the Theme singleton.
class ThemesController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Themes)
    QML_SINGLETON
    /// {id, name, editable} for every theme, built-in first.
    Q_PROPERTY(QVariantList available READ available NOTIFY changed)
    /// The theme in use, as a built-in id or a file path.
    Q_PROPERTY(QString current READ current NOTIFY changed)
    /// Whether the theme in use is one of the user's own, which can be edited.
    Q_PROPERTY(bool editable READ editable NOTIFY changed)
    /// The colors the editor offers, in order. Constant, so the editor's rows
    /// stay put while their colors change.
    Q_PROPERTY(QStringList colorKeys READ colorKeys CONSTANT)
    /// key to color for the theme in use.
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    /// Why the last action failed, or empty.
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    static ThemesController *instance();
    static ThemesController *create(QQmlEngine *, QJSEngine *);

    /// Applies the theme Settings remembers. A theme that no longer loads is
    /// forgotten, so the default takes over.
    void restore();

    /// Applies a theme for this run only, as --theme and CALLIE_THEME do,
    /// leaving the remembered choice alone. Returns the load errors.
    QStringList applyForThisRun(const QString &idOrPath);

    [[nodiscard]] QVariantList available() const;
    [[nodiscard]] QString current() const;
    [[nodiscard]] bool editable() const;
    [[nodiscard]] QStringList colorKeys() const { return ThemeLibrary::colorKeys(); }
    [[nodiscard]] QVariantMap colors() const;
    [[nodiscard]] QString error() const { return m_error; }

    Q_INVOKABLE bool use(const QString &idOrPath);
    /// Switches to an editable copy of the theme in use, made if needed.
    Q_INVOKABLE bool customize();
    Q_INVOKABLE bool setColor(const QString &key, const QColor &color);
    Q_INVOKABLE bool importFrom(const QUrl &file);
    Q_INVOKABLE bool exportTo(const QUrl &file);
    /// Deletes the user theme in use and goes back to the default.
    Q_INVOKABLE bool removeCurrent();
    Q_INVOKABLE void useDefault();

Q_SIGNALS:
    void changed();
    void errorChanged();

private:
    explicit ThemesController(QObject *parent);
    [[nodiscard]] Settings *settings() const;
    bool fail(const QString &message);
    void clearError();

    ThemeLibrary m_library;
    /// The theme on screen when it is not the remembered one.
    QString m_override;
    QString m_error;
};

} // namespace callie
