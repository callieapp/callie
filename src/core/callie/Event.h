#pragma once

#include <QColor>
#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace callie {

/// Someone invited to an event, with their answer: "accepted", "tentative",
/// "declined" or "needsAction".
struct Guest
{
    QString email;
    QString name;
    QString response;
    bool organizer = false;
    bool self = false;

    friend bool operator==(const Guest &, const Guest &) = default;
};

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
    Q_PROPERTY(QUrl joinUrl READ joinUrl)
    Q_PROPERTY(QColor color MEMBER color)
    Q_PROPERTY(bool declined MEMBER declined)

public:
    QString uid;
    QDateTime recurrenceId;
    QString calendarId;
    QString summary;
    QString description;
    QString location;
    /// The provider's own video call, which the editor's "Video call" turns on and off.
    QUrl conferenceUrl;
    QDateTime start;
    QDateTime end;
    bool allDay = false;
    /// The IANA zone the event was written in, whatever zone `start` is shown
    /// in; a repeating event repeats on its clock. Empty when unknown.
    QString zone;
    QColor color;
    /// The user was invited and said no.
    bool declined = false;
    /// The user's answer: "accepted", "tentative", "declined", "needsAction",
    /// or empty when they are not an invited guest.
    QString responseStatus;
    /// The other guests' email addresses.
    QStringList attendees;
    /// Everyone invited, the user included, organizer first.
    QList<Guest> guests;
    /// The repeat rule of the series it belongs to, as RFC 5545 lines; empty
    /// for an event that does not repeat.
    QStringList recurrence;
    /// The source's id for this one occurrence, and for the series it belongs
    /// to (empty for a one-off event), which actions on it need.
    QString eventId;
    QString seriesId;
    /// Whether the user may change or delete it, and answer the invitation.
    bool canEdit = false;
    bool canRespond = false;
    /// Minutes before the start to remind. Only when `remindersKnown` is the
    /// list the source's own, so an empty one means no reminders; otherwise
    /// Callie's default applies.
    QList<int> reminders;
    bool remindersKnown = false;

    /// The call Join opens: the provider's own, else the first call link in
    /// the place or notes, such as a Zoom link pasted into an invitation.
    [[nodiscard]] QUrl joinUrl() const;

    /// Where an overlapping event sits, filled in by the view model: `depth`
    /// steps it in over earlier events, and `lane` of `laneCount` places it
    /// beside events that start at about the same time.
    int depth = 0;
    int lane = 0;
    int laneCount = 1;

    [[nodiscard]] bool isValid() const { return !uid.isEmpty() && start.isValid(); }
};

} // namespace callie

Q_DECLARE_METATYPE(callie::Event)
