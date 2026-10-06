#pragma once

#include "Event.h"

#include <QDate>
#include <QDateTime>
#include <QFuture>
#include <QObject>
#include <QString>
#include <QTimeZone>

namespace callie {

/// Metadata for one calendar within a source (e.g. one of several Google
/// calendars on an account).
struct CalendarInfo
{
    QString id;
    QString displayName;
    QColor color;
    bool writable = false;
    bool enabled = true;
    /// The account the calendar belongs to, for grouping; empty if there is one.
    QString account = {};
};

/// What a source holds for a range, read at one moment: its calendars and the
/// occurrences in the range.
struct SourceSnapshot
{
    QList<Event> events;
    QList<CalendarInfo> calendars;
};

/// Backend-agnostic interface for anything that can supply events: Google
/// Calendar API, CalDAV, or a local ICS file. Implementations own their own
/// caching and are expected to answer `eventsBetween` from local state.
class CalendarSource : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool syncing READ syncing NOTIFY statusChanged)
    Q_PROPERTY(QDateTime lastSynced READ lastSynced NOTIFY statusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY statusChanged)

public:
    using QObject::QObject;
    ~CalendarSource() override = default;

    [[nodiscard]] virtual QString sourceId() const = 0;
    [[nodiscard]] virtual QList<CalendarInfo> calendars() const = 0;

    /// Occurrences overlapping [from, to), expanded from recurrence rules and
    /// converted into `tz`. Must not block on the network.
    [[nodiscard]] virtual QList<Event> eventsBetween(const QDateTime &from, const QDateTime &to,
                                                     const QTimeZone &tz) const = 0;

    /// calendars() and eventsBetween() together, without blocking the caller.
    /// Sources that read from disk answer from another thread; the default
    /// answers at once.
    [[nodiscard]] virtual QFuture<SourceSnapshot> load(const QDateTime &from, const QDateTime &to,
                                                       const QTimeZone &tz) const
    {
        return QtFuture::makeReadyValueFuture(
            SourceSnapshot{eventsBetween(from, to, tz), calendars()});
    }

    /// Kick off a background refresh. Emits `changed` when new data lands.
    virtual void refresh() = 0;

    /// Sync status for the title bar. Sources that never sync keep the defaults.
    [[nodiscard]] virtual bool syncing() const { return false; }
    [[nodiscard]] virtual QDateTime lastSynced() const { return {}; }
    [[nodiscard]] virtual QString lastError() const { return {}; }

Q_SIGNALS:
    void changed();
    void errorOccurred(const QString &message);
    void statusChanged();
};

} // namespace callie
