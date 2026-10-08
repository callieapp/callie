#pragma once

#include "callie/CalendarSource.h"

#include <QCoreApplication>
#include <QTextStream>
#include <QTimeZone>

#include <functional>
#include <optional>

namespace callie {

class Settings;

namespace cli {

/// What `callie edit` was told to change; unset options stay as they are.
struct EditOptions
{
    std::optional<QString> title;
    std::optional<QString> where;
    std::optional<QString> notes;
    /// A day and time as `callie add` reads them, "2026-10-08 15:00", or a
    /// time alone for the event's own day.
    std::optional<QString> start;
    std::optional<QString> end;
    std::optional<bool> allDay;
    /// A Repeat choice: none, daily, weekdays, weekly, monthly, monthlyWeekday or yearly.
    std::optional<QString> repeat;
    std::optional<QStringList> guests;
    std::optional<bool> video;
};

/// The event commands, on any calendar source, so they can be tried on
/// sample data. Each writes what it found to `out` and problems to `err`,
/// and ends with an exit code: 0 for success, 1 for a failure, 2 for a
/// mistake in how it was asked.
class Commands
{
    Q_DECLARE_TR_FUNCTIONS(Commands)

public:
    using Done = std::function<void(int code)>;

    Commands(CalendarSource &source, Settings &settings, QTextStream &out, QTextStream &err,
             QDateTime now, QTimeZone zone);

    /// Colors the output for a terminal.
    void setColor(bool color) { m_color = color; }

    /// The next `days` days, by day. With `json`, a JSON array whose ids are
    /// what the other commands take.
    int agenda(int days, bool json);
    /// Events with every word of `query`, a year either side, upcoming first.
    int search(const QString &query, bool json);
    /// Invitations not answered yet, in the next 90 days.
    int invites(bool json);

    /// Creates an event from a line such as "Lunch with Alex tomorrow 12-1pm".
    void add(const QString &text, const QString &calendarId, const Done &done);
    /// Changes the event; `scope` is "this", "following" or "all".
    void edit(const QString &id, const EditOptions &options, const QString &scope,
              const Done &done);
    void remove(const QString &id, const QString &scope, const Done &done);
    /// `answer` is yes, maybe or no.
    void respond(const QString &id, const QString &answer, const QString &scope, const Done &done);
    /// A copy of the event, at `start` if given, else at the same time.
    void duplicate(const QString &id, const std::optional<QString> &start, const Done &done);

    /// With no name, every setting and its value; with a name, its value; with
    /// a value too, changes it quietly. The app reads settings when it starts.
    static int settings(Settings &settings, QTextStream &out, QTextStream &err, const QString &name,
                        const std::optional<QString> &value);
    /// Hides, shows, renames, recolors or resets the look of a calendar, given
    /// by its id or name: `action` is hide, show, rename, color or reset.
    int calendarLook(const QString &action, const QString &calendar, const QString &value);
    /// The name Callie shows for an account; empty goes back to its own.
    int renameAccount(const QString &account, const QString &name);

    /// Reads a day and time: "2026-10-08 15:00", "2026-10-08", "15:00" on
    /// `day`, or anything `callie add` understands, such as "friday 3pm".
    [[nodiscard]] std::optional<QDateTime> when(const QString &text, QDate day) const;

private:
    [[nodiscard]] std::optional<Event> find(const QString &id);
    [[nodiscard]] std::optional<EditScope> scopeOf(const QString &scope);
    void print(const QList<Event> &events, bool json, bool byDay);
    /// The id the commands take: the calendar, a slash, and the event.
    [[nodiscard]] static QString reference(const Event &event);
    [[nodiscard]] QString writableCalendar(const QString &wanted) const;
    void report(const Outcome &outcome, const Done &done);

    CalendarSource &m_source;
    Settings &m_settings;
    QTextStream &m_out;
    QTextStream &m_err;
    QDateTime m_now;
    QTimeZone m_zone;
    bool m_color = false;
};

} // namespace cli
} // namespace callie
