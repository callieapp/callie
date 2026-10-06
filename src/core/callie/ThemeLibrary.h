#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

namespace callie {

/// The user's own themes: TOML files in one folder that Callie can copy,
/// edit a color at a time, import and export. Built-in themes are never
/// changed; customizing one starts a copy here.
class ThemeLibrary
{
public:
    /// `folder` holds the user's theme files and is created on first write.
    explicit ThemeLibrary(QString folder);

    /// `$XDG_CONFIG_HOME/callie/themes`
    [[nodiscard]] static QString defaultFolder();

    [[nodiscard]] QString folder() const { return m_folder; }

    /// Paths of the user's theme files, sorted by name.
    [[nodiscard]] QStringList themes() const;

    /// The [colors] keys a theme can set, in the order an editor lists them.
    [[nodiscard]] static QStringList colorKeys();

    /// A theme's display name: its [theme] name, or the file name.
    [[nodiscard]] static QString displayName(const QString &idOrPath);

    /// Copies a built-in theme or a theme file into the folder under a fresh
    /// name and returns the new path, or empty with error() set.
    QString copy(const QString &idOrPath, const QString &name);

    /// Sets one [colors] key in a theme file, keeping everything else as it
    /// was. Fails unless `path` is in the folder.
    bool setColor(const QString &path, const QString &key, const QColor &color);

    /// Copies a theme file in after checking that it loads; returns its new path.
    QString importTheme(const QString &source);

    /// Writes a theme, built-in or not, to `destination`.
    bool exportTheme(const QString &idOrPath, const QString &destination);

    /// Deletes one of the user's theme files.
    bool remove(const QString &path);

    [[nodiscard]] QString error() const { return m_error; }

private:
    [[nodiscard]] bool owns(const QString &path) const;
    QString freshPath(const QString &name) const;
    bool fail(const QString &message);

    QString m_folder;
    QString m_error;
};

} // namespace callie
