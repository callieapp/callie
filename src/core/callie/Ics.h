#pragma once

#include "Event.h"
#include "EventEdit.h"
#include "QuickAdd.h"

#include <KCalendarCore/Event>

#include <QByteArray>
#include <QByteArrayView>
#include <QColor>
#include <QList>
#include <QTimeZone>

#include <optional>

/// iCalendar (RFC 5545) events, as kept in .ics files: reading and writing
/// them, expanding a series into occurrences, and making the changes Callie
/// offers to one. A "group" is one UID's events: the series, or a one-off,
/// and the occurrences changed on their own.
namespace callie::ics {

using Group = QList<KCalendarCore::Event::Ptr>;

/// The VEVENTs in iCalendar text, or empty with `error` set when it cannot
/// be read. Other components, such as to-dos, are left out.
[[nodiscard]] Group parse(QByteArrayView text, QString *error = nullptr);

/// One VCALENDAR holding `events`, with the time zones they use.
[[nodiscard]] QByteArray write(const Group &events);

/// `events` split by UID, keeping their order.
[[nodiscard]] QList<Group> byUid(const Group &events);

/// What expanding a calendar's events needs to know about the calendar.
struct Calendar
{
    QString id;
    QColor color;
    bool writable = false;
    /// The user's address, which picks out their own answer to invitations.
    QString self = {};
};

/// The occurrences of one group overlapping [from, to), with all-day events
/// starting at midnight in `viewZone`.
[[nodiscard]] QList<Event> expand(const Group &group, const Calendar &calendar,
                                  const QDateTime &from, const QDateTime &to,
                                  const QTimeZone &viewZone);

/// A new event from a draft, with `uid` as its UID.
[[nodiscard]] KCalendarCore::Event::Ptr fromDraft(const EventDraft &draft, const QString &uid);

/// The group after a change, or nothing when the change cannot be made, with
/// `error` saying why. An empty group means the event is gone.
struct Changed
{
    Group group;
    /// A new series split off by a change to this and following occurrences.
    Group split = {};
};

[[nodiscard]] std::optional<Changed> remove(const Group &group, const Event &occurrence,
                                            bool wholeSeries, QString *error);
[[nodiscard]] std::optional<Changed> move(const Group &group, const Event &occurrence,
                                          const QDateTime &start, const QDateTime &end,
                                          bool wholeSeries, QString *error);
/// `newUid` names the series a ThisAndFollowing change splits off.
[[nodiscard]] std::optional<Changed> update(const Group &group, const Event &occurrence,
                                            const EventEdit &edit, EditScope scope,
                                            const QString &newUid, QString *error);
/// Sets the user's answer, "accepted", "tentative" or "declined", as the
/// guest `self`.
[[nodiscard]] std::optional<Changed> respond(const Group &group, const Event &occurrence,
                                             const QString &status, bool wholeSeries,
                                             const QString &self, QString *error);

} // namespace callie::ics
