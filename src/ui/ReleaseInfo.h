#pragma once

#include <QDateTime>
#include <QObject>
#include <QQmlEngine>
#include <QUrl>

namespace callie {

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
    [[nodiscard]] QUrl history() const;

    /// True once, the first time a version starts after another one ran;
    /// a first run and later starts say no. Records this version either way.
    Q_INVOKABLE bool takeUpdateNotice();

private:
    QString m_notesVersion;
    QDateTime m_notesDate;
    QString m_notes;
};

} // namespace callie
