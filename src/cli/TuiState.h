#pragma once

#include "callie/CalendarSource.h"

#include <QDate>
#include <QObject>
#include <QPointer>
#include <QUrl>

#include <functional>
#include <optional>

namespace callie {
class Settings;
}

namespace callie::cli {

class Commands;

/// What the TUI shows and how keys change it, apart from drawing it, so it can
/// be tested without a terminal. The keys follow the app's vi mode.
class TuiState : public QObject
{
    Q_OBJECT

public:
    enum class View { Day, Week, Agenda };
    /// What keys go to: moving around, an event's details, a line being
    /// typed, a list of found events, or the calendars.
    enum class Mode { Browse, Details, Typing, Found, Calendars };
    /// What a line being typed is for.
    enum class Typing { NewEvent, Search };
    using Now = std::function<QDateTime()>;

    TuiState(CalendarSource &source, Settings &settings, Now now, QObject *parent = nullptr);

    [[nodiscard]] View view() const { return m_view; }
    /// The day in focus: the one shown, or inside the week or agenda shown.
    [[nodiscard]] QDate day() const { return m_day; }
    [[nodiscard]] QDate rangeStart() const;
    [[nodiscard]] int rangeDays() const;
    [[nodiscard]] QDateTime now() const { return m_now(); }
    [[nodiscard]] QDate today() const;
    /// The events shown: by day, all-day ones first, then by start.
    [[nodiscard]] const QList<Event> &events() const { return m_events; }
    /// The day `event` is listed under: its first day in the range.
    [[nodiscard]] QDate dayOf(const Event &event) const;
    /// The selected event's place in events(), or -1.
    [[nodiscard]] int selected() const { return m_selected; }
    [[nodiscard]] bool helpShown() const { return m_help; }
    [[nodiscard]] Mode mode() const { return m_mode; }
    [[nodiscard]] Typing typing() const { return m_typing; }
    /// The line being typed.
    [[nodiscard]] QString input() const { return m_input; }
    /// What a search or the invitations found, and which is selected.
    [[nodiscard]] const QList<Event> &found() const { return m_found; }
    [[nodiscard]] QString foundTitle() const { return m_foundTitle; }
    [[nodiscard]] int foundSelected() const { return m_foundSelected; }
    /// The calendars, for showing or hiding them, and which is selected.
    [[nodiscard]] QList<CalendarInfo> calendars() const;
    [[nodiscard]] int calendarSelected() const { return m_calendarSelected; }
    [[nodiscard]] bool calendarShown(const CalendarInfo &calendar) const;
    /// The event whose details are open, or the selected one.
    [[nodiscard]] std::optional<Event> current() const;
    /// What the last action did, or why it could not.
    [[nodiscard]] QString message() const { return m_message; }
    [[nodiscard]] bool messageIsError() const { return m_messageIsError; }
    /// "leader" or "g" while waiting for the key that finishes a command.
    [[nodiscard]] QString pending() const { return m_pending; }
    /// The sync state in a few words, and whether it is an error.
    [[nodiscard]] QString syncStatus() const;
    [[nodiscard]] bool syncFailed() const;
    [[nodiscard]] bool quitRequested() const { return m_quit; }

    /// A key typed: a character, or Left, Right, Up, Down, Home, End, Enter,
    /// Escape or Tab. Returns whether it did anything.
    bool press(const QString &key);
    /// Reads the events again, as after a sync.
    void reload();

Q_SIGNALS:
    void changed();
    /// The user asked to open a call or a link.
    void openRequested(const QUrl &url);

private:
    void step(int direction);
    void setView(View view);
    void selectFirstOf(QDate day);
    bool pressBrowsing(const QString &key);
    bool pressDetails(const QString &key);
    bool pressTyping(const QString &key);
    bool pressFound(const QString &key);
    bool pressCalendars(const QString &key);
    void submit();
    /// What `what` lists, asked of the CLI's commands so both list the same.
    [[nodiscard]] QList<Event> ask(const std::function<QList<Event>(const Commands &)> &what) const;
    void showFound(const QString &title, QList<Event> events);
    void jumpTo(const Event &event);
    /// Answers or deletes through the commands the CLI runs, so the TUI
    /// behaves as `callie respond` and `callie delete` do.
    void act(const std::function<void(Commands &, const std::function<void(int)> &)> &run);
    void say(const QString &message, bool error = false);

    QPointer<CalendarSource> m_source;
    Settings &m_settings;
    Now m_now;
    View m_view = View::Day;
    QDate m_day;
    QList<Event> m_events;
    int m_selected = -1;
    bool m_help = false;
    bool m_quit = false;
    QString m_pending;
    Mode m_mode = Mode::Browse;
    Typing m_typing = Typing::NewEvent;
    QString m_input;
    QList<Event> m_found;
    QString m_foundTitle;
    int m_foundSelected = -1;
    int m_calendarSelected = 0;
    QString m_message;
    bool m_messageIsError = false;
    /// The last change made here, for undo.
    QString m_lastChange;
};

} // namespace callie::cli
