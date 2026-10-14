#pragma once

#include "Ics.h"

#include <QJsonObject>
#include <QStringList>

#include <optional>

/// Microsoft Graph events as iCalendar, and back. Callie keeps an Outlook
/// calendar as .ics files, so these are the two directions of its sync.
namespace callie::graph {

/// One event as Graph lists it: a single event, or a series master with the
/// occurrences changed on their own and those cancelled.
struct Series
{
    QJsonObject event;
    QList<QJsonObject> exceptions;
    /// Graph's occurrenceId values, which end in the occurrence's date.
    QStringList cancelled;
};

/// A Graph time zone, by Windows or IANA name, as Qt knows it; UTC when unknown.
[[nodiscard]] QTimeZone zoneFor(const QString &name);
/// A Graph dateTimeTimeZone, as {dateTime, timeZone}.
[[nodiscard]] QDateTime timeOf(const QJsonObject &dateTimeTimeZone);

/// The series as iCalendar: the master, or the single event, with its
/// changed occurrences under RECURRENCE-ID and the cancelled ones as EXDATEs.
[[nodiscard]] ics::Group toIcs(const Series &series);

/// The RRULE line for a Graph patternedRecurrence of a series starting at
/// `start`; empty when Callie cannot read the pattern.
[[nodiscard]] QString ruleFor(const QJsonObject &recurrence, const QDateTime &start, bool allDay);
/// Graph's patternedRecurrence for the event's repeat, or nothing with `error`
/// set when Outlook cannot repeat that way.
[[nodiscard]] std::optional<QJsonObject> recurrenceFor(const KCalendarCore::Event &event,
                                                       QString *error);

/// The fields Graph takes to make or change the event: subject, body,
/// location, times, availability, guests and, for a series, its repeat.
/// Nothing, with `error` set, when Outlook cannot hold the event as it is.
[[nodiscard]] std::optional<QJsonObject> fieldsFor(const KCalendarCore::Event &event,
                                                   QString *error);

} // namespace callie::graph
