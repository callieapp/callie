#pragma once

#include <QColor>
#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QUrl>

namespace callie {

/// One occurrence of a calendar entry, already expanded from any recurrence
/// rule. `recurrenceId` distinguishes occurrences sharing a `uid`.
struct Event
{
    Q_GADGET
    Q_PROPERTY(QString uid MEMBER uid)
    Q_PROPERTY(QString summary MEMBER summary)
    Q_PROPERTY(QDateTime start MEMBER start)
    Q_PROPERTY(QDateTime end MEMBER end)
    Q_PROPERTY(bool allDay MEMBER allDay)
    Q_PROPERTY(QString location MEMBER location)
    Q_PROPERTY(QUrl conferenceUrl MEMBER conferenceUrl)
    Q_PROPERTY(QColor color MEMBER color)
    Q_PROPERTY(bool declined MEMBER declined)

public:
    QString uid;
    QDateTime recurrenceId;
    QString calendarId;
    QString summary;
    QString description;
    QString location;
    QUrl conferenceUrl;
    QDateTime start;
    QDateTime end;
    bool allDay = false;
    QColor color;
    /// The user was invited and said no.
    bool declined = false;

    /// Lane assignment for overlapping events, filled in by the view model.
    int lane = 0;
    int laneCount = 1;

    [[nodiscard]] bool isValid() const { return !uid.isEmpty() && start.isValid(); }
};

} // namespace callie

Q_DECLARE_METATYPE(callie::Event)
