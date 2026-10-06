#pragma once

#include "Times.h"

#include <QDate>
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QTimeZone>

namespace callie {

/// The user's preferences, kept as INI in the config directory and shared by
/// the app's views. Every setter saves at once and emits only on change.
class Settings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(TimeFormat timeFormat READ timeFormat WRITE setTimeFormat NOTIFY timeFormatChanged)
    Q_PROPERTY(bool use24Hour READ use24Hour NOTIFY timeFormatChanged)
    Q_PROPERTY(QString timeZoneId READ timeZoneId WRITE setTimeZoneId NOTIFY timeZoneChanged)
    Q_PROPERTY(bool showDeclined READ showDeclined WRITE setShowDeclined NOTIFY showDeclinedChanged)
    Q_PROPERTY(bool dimPast READ dimPast WRITE setDimPast NOTIFY dimPastChanged)
    Q_PROPERTY(bool widenToday READ widenToday WRITE setWidenToday NOTIFY widenTodayChanged)
    Q_PROPERTY(QStringList hiddenCalendars READ hiddenCalendars NOTIFY hiddenCalendarsChanged)
    Q_PROPERTY(callie::Times *times READ times NOTIFY timesChanged)
    /// A built-in theme id or a theme file path; empty is the default theme.
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QStringList collapsedAccounts READ collapsedAccounts NOTIFY collapsedAccountsChanged)
    /// The calendar new events went into last, by CalendarInfo::id.
    Q_PROPERTY(QString newEventCalendar READ newEventCalendar WRITE setNewEventCalendar NOTIFY
                   newEventCalendarChanged)
    /// The calendar view last shown: "day", "week", "month" or "agenda".
    Q_PROPERTY(QString view READ view WRITE setView NOTIFY viewChanged)

public:
    enum class TimeFormat { Locale, TwentyFourHour, TwelveHour };
    Q_ENUM(TimeFormat)

    explicit Settings(const QString &path, QObject *parent = nullptr);

    /// `$XDG_CONFIG_HOME/callie/settings.ini`
    [[nodiscard]] static QString defaultPath();

    [[nodiscard]] TimeFormat timeFormat() const { return m_timeFormat; }
    void setTimeFormat(TimeFormat format);
    /// The time format resolved against the locale.
    [[nodiscard]] bool use24Hour() const;

    /// An IANA id, or empty to follow the system zone.
    [[nodiscard]] QString timeZoneId() const { return m_timeZoneId; }
    void setTimeZoneId(const QString &id);
    [[nodiscard]] QTimeZone timeZone() const;

    [[nodiscard]] bool showDeclined() const { return m_showDeclined; }
    void setShowDeclined(bool show);

    [[nodiscard]] bool dimPast() const { return m_dimPast; }
    void setDimPast(bool dim);

    [[nodiscard]] bool widenToday() const { return m_widenToday; }
    void setWidenToday(bool widen);

    /// Calendars the user hid in Callie, by CalendarInfo::id.
    [[nodiscard]] QStringList hiddenCalendars() const { return m_hiddenCalendars; }
    Q_INVOKABLE void setCalendarVisible(const QString &id, bool visible);

    /// Times in the chosen zone and clock format. A JS Date only knows the
    /// system zone, so QML formats and places times through this.
    [[nodiscard]] Times *times() const { return m_times; }
    /// Accounts whose calendars the sidebar folds away.
    [[nodiscard]] QStringList collapsedAccounts() const { return m_collapsedAccounts; }
    Q_INVOKABLE void setAccountCollapsed(const QString &account, bool collapsed);

    [[nodiscard]] QString theme() const { return m_theme; }
    void setTheme(const QString &idOrPath);

    [[nodiscard]] QString newEventCalendar() const { return m_newEventCalendar; }
    void setNewEventCalendar(const QString &id);

    [[nodiscard]] QString view() const { return m_view; }
    void setView(const QString &view);

    /// Every IANA zone id, for the zone picker.
    Q_INVOKABLE static QStringList availableTimeZones();

    /// Forgets every preference.
    Q_INVOKABLE void reset();

Q_SIGNALS:
    void timeFormatChanged();
    void timeZoneChanged();
    void showDeclinedChanged();
    void dimPastChanged();
    void widenTodayChanged();
    void hiddenCalendarsChanged();
    void timesChanged();
    void themeChanged();
    void collapsedAccountsChanged();
    void viewChanged();
    void newEventCalendarChanged();

private:
    void load();
    void rebuildTimes();

    QSettings m_store;
    TimeFormat m_timeFormat = TimeFormat::Locale;
    QString m_timeZoneId;
    bool m_showDeclined = true;
    bool m_dimPast = true;
    bool m_widenToday = false;
    QStringList m_hiddenCalendars;
    Times *m_times = nullptr;
    QString m_theme;
    QStringList m_collapsedAccounts;
    QString m_view;
    QString m_newEventCalendar;
};

} // namespace callie
