#pragma once

#include "VdirSource.h"

namespace callie {

/// The calendars on this computer: one vdir folder each under a root folder.
/// A folder added there by hand, or by vdirsyncer, shows as a calendar too.
class LocalCalendars : public VdirSource
{
    Q_OBJECT

public:
    explicit LocalCalendars(QString root, QObject *parent = nullptr);

    /// The CalendarInfo::account its calendars are grouped under.
    static constexpr QLatin1StringView kAccount{"local"};

    /// `$XDG_DATA_HOME/callie/calendars`
    [[nodiscard]] static QString defaultRoot();
    [[nodiscard]] QString root() const { return m_root; }

    /// The id a folder's calendar has.
    [[nodiscard]] static QString idFor(const QString &folderName);

    /// Makes an empty calendar; returns its id, or empty with `error` set.
    QString create(const QString &name, const QColor &color, QString *error);
    /// Moves the calendar's folder to the trash, so it can be got back.
    bool remove(const QString &id, QString *error);

    /// Looks for calendars added or removed, and reads them again.
    void refresh() override;

private:
    void scan();

    QString m_root;
    QFileSystemWatcher m_rootWatcher;
};

} // namespace callie
