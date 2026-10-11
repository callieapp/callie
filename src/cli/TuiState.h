#pragma once

#include "callie/CalendarSource.h"

#include <QDate>
#include <QObject>
#include <QPointer>

#include <functional>
#include <optional>

namespace callie {
class Settings;
}

namespace callie::cli {

/// What the TUI shows and how keys change it, apart from drawing it, so it can
/// be tested without a terminal. The keys follow the app's vi mode.
class TuiState : public QObject
{
    Q_OBJECT

public:
    enum class View { Day, Week, Agenda };
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

private:
    void step(int direction);
    void setView(View view);
    void selectFirstOf(QDate day);

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
};

} // namespace callie::cli
