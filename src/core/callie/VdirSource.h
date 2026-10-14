#pragma once

#include "CalendarSource.h"
#include "Ics.h"

#include <QFileSystemWatcher>
#include <QHash>
#include <QMutex>
#include <QThreadPool>
#include <QTimer>

namespace callie {

/// Calendars kept as vdir folders, as khal and vdirsyncer keep them: one .ics
/// file per event, and the calendar's name and color in files named
/// "displayname" and "color". Changes made outside Callie show as they land.
class VdirSource : public CalendarSource
{
    Q_OBJECT

public:
    struct Folder
    {
        /// The CalendarInfo::id its calendar has.
        QString id;
        QString path;
        bool writable = true;
        /// The account it belongs to, for grouping; empty for this computer's.
        QString account = {};
        /// The user's address there, which picks out their own answers.
        QString self = {};
    };

    explicit VdirSource(QObject *parent = nullptr);
    ~VdirSource() override;

    void setFolders(const QList<Folder> &folders);
    [[nodiscard]] QList<Folder> folders() const { return m_folders; }

    [[nodiscard]] QString sourceId() const override { return QStringLiteral("vdir"); }
    [[nodiscard]] QList<CalendarInfo> calendars() const override;
    [[nodiscard]] QList<Event> eventsBetween(const QDateTime &from, const QDateTime &to,
                                             const QTimeZone &tz) const override;
    /// Reads on a background thread, parsing only the files that changed.
    [[nodiscard]] QFuture<SourceSnapshot> load(const QDateTime &from, const QDateTime &to,
                                               const QTimeZone &tz) const override;
    void refresh() override;

    void createEvent(const EventDraft &draft, Created done) override;
    void importEvents(const QString &calendarId, const QByteArray &data, Imported done) override;
    [[nodiscard]] QByteArray exportEvents(const QString &calendarId, const QString &uid,
                                          QString *error) const override;
    void respond(const Event &event, const QString &status, bool wholeSeries,
                 Created done) override;
    void deleteEvent(const Event &event, bool wholeSeries, Created done) override;
    void moveEvent(const Event &event, const QDateTime &start, const QDateTime &end,
                   bool wholeSeries, Created done) override;
    void updateEvent(const Event &event, const EventEdit &edit, EditScope scope,
                     Created done) override;

    /// Writes `events` into the calendar, one file per UID. An event already
    /// there, as from importing the same file twice, is replaced. Returns how
    /// many events it wrote, or -1 with `error` set.
    int importEvents(const QString &calendarId, const ics::Group &events, QString *error);

    /// The colors calendars without a "color" file are given.
    [[nodiscard]] static QList<QColor> palette();
    /// The color a calendar without a "color" file is shown in.
    [[nodiscard]] static QColor defaultColor(const QString &id);

protected:
    /// A file in `folder` was written, or removed, by a change made in Callie.
    virtual void fileChanged(const Folder &folder, const QString &fileName, bool removed);
    /// Makes the folder hold `events` and nothing else, one file per UID, as
    /// a copy of a calendar kept elsewhere.
    bool mirror(const Folder &folder, const ics::Group &events, QString *error);
    /// Writes or removes one file as it is elsewhere, such as on a server,
    /// without telling fileChanged.
    bool writeFile(const Folder &folder, const QString &fileName, const QByteArray &data,
                   QString *error);
    bool removeFile(const Folder &folder, const QString &fileName, QString *error);

private:
    /// A parsed file, kept until the file changes.
    struct Parsed
    {
        QDateTime modified;
        qint64 size = -1;
        ics::Group events;
    };
    /// One folder's events by UID, and the file each UID is in.
    struct Contents
    {
        QList<ics::Group> groups;
        QHash<QString, QString> files;
    };

    [[nodiscard]] static QList<CalendarInfo> calendarsOf(const QList<Folder> &folders);
    [[nodiscard]] Contents read(const Folder &folder) const;
    /// Safe on any thread, given its own copy of the folders.
    [[nodiscard]] SourceSnapshot snapshot(const QList<Folder> &folders, const QDateTime &from,
                                          const QDateTime &to, const QTimeZone &tz) const;
    [[nodiscard]] const Folder *folder(const QString &id) const;
    /// Finds the event's folder and group, or tells `done` why not.
    bool find(const Event &event, const Created &done, const Folder **folder, ics::Group *group,
              QString *fileName) const;
    /// A change to one UID's events, or nothing with the error set.
    using Change = std::function<std::optional<ics::Changed>(const ics::Group &, QString *)>;
    /// Writes a change found by `change`, telling `done` how it went.
    void apply(const Event &event, const Created &done, const Change &change);
    /// Puts `group` in place of the events of `uid` in the file, removing the
    /// file once nothing is left in it.
    bool save(const Folder &folder, const QString &fileName, const QString &uid,
              const ics::Group &group, QString *error);
    void watch();

    QList<Folder> m_folders;
    mutable QMutex m_lock;
    mutable QHash<QString, Parsed> m_parsed;
    QFileSystemWatcher m_watcher;
    /// Editors write in bursts; views reload once they settle.
    QTimer m_settle;
    /// Last, so it is the first to go and waits for reads under way.
    mutable QThreadPool m_readers;
};

} // namespace callie
