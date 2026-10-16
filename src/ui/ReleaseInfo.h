#pragma once

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QTimer>
#include <QUrl>

namespace callie {

class Settings;
class UpdateCheck;

/// What the About and "What's new?" popups say: this build's version, the
/// newest notes in the bundled CHANGELOG.md, and where to read more.
class ReleaseInfo : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Release)
    QML_SINGLETON
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString notesVersion READ notesVersion CONSTANT)
    /// The day those notes were released, as a local midnight for QML.
    Q_PROPERTY(QDateTime notesDate READ notesDate CONSTANT)
    /// The notes themselves, Markdown.
    Q_PROPERTY(QString notes READ notes CONSTANT)
    Q_PROPERTY(QUrl website READ website CONSTANT)
    Q_PROPERTY(QUrl privacy READ privacy CONSTANT)
    Q_PROPERTY(QUrl terms READ terms CONSTANT)
    Q_PROPERTY(QUrl source READ source CONSTANT)
    /// Every version's notes.
    Q_PROPERTY(QUrl history READ history CONSTANT)
    /// Where to support Callie's development.
    Q_PROPERTY(QUrl support READ support CONSTANT)
    /// A development build, kept apart from an installed Callie.
    Q_PROPERTY(bool devel READ devel CONSTANT)
    /// A Flatpak, which Flathub keeps up to date.
    Q_PROPERTY(bool flatpak READ flatpak CONSTANT)
    /// A newer release that is out, such as "0.3.1", or empty.
    Q_PROPERTY(QString update READ update NOTIFY updateChanged)
    /// That release's page.
    Q_PROPERTY(QUrl updatePage READ updatePage NOTIFY updateChanged)

public:
    explicit ReleaseInfo(QObject *parent = nullptr);

    [[nodiscard]] QString version() const;
    [[nodiscard]] QString notesVersion() const { return m_notesVersion; }
    [[nodiscard]] QDateTime notesDate() const { return m_notesDate; }
    [[nodiscard]] QString notes() const { return m_notes; }
    [[nodiscard]] QUrl website() const;
    [[nodiscard]] QUrl privacy() const;
    [[nodiscard]] QUrl terms() const;
    [[nodiscard]] QUrl source() const;
    [[nodiscard]] QUrl support() const;
    [[nodiscard]] bool devel() const;
    [[nodiscard]] bool flatpak() const;
    [[nodiscard]] QUrl history() const;
    [[nodiscard]] QString update() const { return m_update; }
    [[nodiscard]] QUrl updatePage() const { return m_updatePage; }

    /// Says when a newer release is out, asking `check` once a day while the
    /// user's settings allow it.
    void watchForUpdates(Settings *settings, UpdateCheck *check);

    /// True once, the first time a version starts after another one ran;
    /// a first run and later starts say no. Records this version either way.
    Q_INVOKABLE bool takeUpdateNotice();

Q_SIGNALS:
    void updateChanged();

private:
    void showUpdate();
    void checkIfDue();

    QPointer<Settings> m_settings;
    QPointer<UpdateCheck> m_check;
    QTimer m_checkTimer;
    QString m_update;
    QUrl m_updatePage;
    QString m_notesVersion;
    QDateTime m_notesDate;
    QString m_notes;
};

} // namespace callie
