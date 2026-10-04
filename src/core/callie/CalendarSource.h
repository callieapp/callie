#pragma once

#include "Event.h"

#include <QDate>
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
};

/// Backend-agnostic interface for anything that can supply events: Google
/// Calendar API, CalDAV, or a local ICS file. Implementations own their own
/// caching and are expected to answer `eventsBetween` from local state.
class CalendarSource : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~CalendarSource() override = default;

    [[nodiscard]] virtual QString sourceId() const = 0;
    [[nodiscard]] virtual QList<CalendarInfo> calendars() const = 0;

    /// Occurrences overlapping [from, to), expanded from recurrence rules and
    /// converted into `tz`. Must not block on the network.
    [[nodiscard]] virtual QList<Event> eventsBetween(const QDateTime &from, const QDateTime &to,
                                                     const QTimeZone &tz) const = 0;

    /// Kick off a background refresh. Emits `changed` when new data lands.
    virtual void refresh() = 0;

Q_SIGNALS:
    void changed();
    void errorOccurred(const QString &message);
};

} // namespace callie
