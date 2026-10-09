#pragma once

#include "Times.h"

#include <QColor>
#include <QDate>
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QTimeZone>
#include <QUrl>
#include <QVariantMap>

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
    /// The week's first day as chosen, 1 (Monday) to 7 (Sunday), or 0 to
    /// follow the region; firstDayOfWeek resolves it.
    Q_PROPERTY(int weekStart READ weekStart WRITE setWeekStart NOTIFY weekChanged)
    Q_PROPERTY(int firstDayOfWeek READ firstDayOfWeek NOTIFY weekChanged)
    /// The week and month views leave out the region's weekend.
    Q_PROPERTY(bool hideWeekends READ hideWeekends WRITE setHideWeekends NOTIFY weekChanged)
    Q_PROPERTY(bool weekNumbers READ weekNumbers WRITE setWeekNumbers NOTIFY weekChanged)
    /// Working hours, as minutes past midnight; the grid shades the rest.
    Q_PROPERTY(int workStart READ workStart WRITE setWorkStart NOTIFY workHoursChanged)
    Q_PROPERTY(int workEnd READ workEnd WRITE setWorkEnd NOTIFY workHoursChanged)
    /// Shades the hours outside working hours in the day and week views.
    Q_PROPERTY(bool showWorkHours READ showWorkHours WRITE setShowWorkHours NOTIFY workHoursChanged)
    Q_PROPERTY(bool viMode READ viMode WRITE setViMode NOTIFY keyboardChanged)
    Q_PROPERTY(QString leaderKey READ leaderKey WRITE setLeaderKey NOTIFY keyboardChanged)
    Q_PROPERTY(int leaderTimeout READ leaderTimeout WRITE setLeaderTimeout NOTIFY keyboardChanged)
    Q_PROPERTY(QStringList hiddenCalendars READ hiddenCalendars NOTIFY hiddenCalendarsChanged)
    Q_PROPERTY(callie::Times *times READ times NOTIFY timesChanged)
    /// A built-in theme id or a theme file path; empty is the default theme.
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QStringList collapsedAccounts READ collapsedAccounts NOTIFY collapsedAccountsChanged)
    /// The user's own name and color for calendars, by CalendarInfo::id, as
    /// {"name", "color"}; either can be missing, which keeps the calendar's own.
    Q_PROPERTY(QVariantMap calendarLooks READ calendarLooks NOTIFY calendarLooksChanged)
    /// The user's own names for accounts, by account id.
    Q_PROPERTY(QVariantMap accountNames READ accountNames NOTIFY accountNamesChanged)
    /// Each account's profile photo, by account, as Google gave it.
    Q_PROPERTY(QVariantMap accountPhotos READ accountPhotos NOTIFY accountPhotosChanged)
    /// The calendar new events went into last, by CalendarInfo::id.
    Q_PROPERTY(QString newEventCalendar READ newEventCalendar WRITE setNewEventCalendar NOTIFY
                   newEventCalendarChanged)
    /// The calendar new events start in, by CalendarInfo::id; empty starts them
    /// in the one used last.
    Q_PROPERTY(QString defaultCalendar READ defaultCalendar WRITE setDefaultCalendar NOTIFY
                   defaultCalendarChanged)
    /// The Callie version that last ran, so the next one can say what is new.
    Q_PROPERTY(QString lastSeenVersion READ lastSeenVersion WRITE setLastSeenVersion NOTIFY
                   lastSeenVersionChanged)
    /// The calendar view last shown: "day", "week", "month" or "agenda".
    Q_PROPERTY(QString view READ view WRITE setView NOTIFY viewChanged)
    /// Desktop notifications for event reminders.
    Q_PROPERTY(bool notify READ notify WRITE setNotify NOTIFY notifyChanged)
    /// Minutes before events that bring no reminders of their own; -1 for none.
    Q_PROPERTY(int reminderMinutes READ reminderMinutes WRITE setReminderMinutes NOTIFY
                   reminderMinutesChanged)
    /// Closing the window leaves Callie running so reminders still come.
    Q_PROPERTY(bool keepRunning READ keepRunning WRITE setKeepRunning NOTIFY keepRunningChanged)

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

    [[nodiscard]] int weekStart() const { return m_weekStart; }
    void setWeekStart(int day);
    [[nodiscard]] int firstDayOfWeek() const;
    [[nodiscard]] bool hideWeekends() const { return m_hideWeekends; }
    void setHideWeekends(bool hide);
    [[nodiscard]] bool weekNumbers() const { return m_weekNumbers; }
    void setWeekNumbers(bool show);

    [[nodiscard]] int workStart() const { return m_workStart; }
    void setWorkStart(int minutes);
    [[nodiscard]] int workEnd() const { return m_workEnd; }
    void setWorkEnd(int minutes);
    [[nodiscard]] bool showWorkHours() const { return m_showWorkHours; }
    void setShowWorkHours(bool show);

    /// Single keys move around and act, as in vi, with a leader key before
    /// the commands that have no key of their own.
    [[nodiscard]] bool viMode() const { return m_viMode; }
    void setViMode(bool on);
    /// "," (the default), ":" or " ".
    [[nodiscard]] QString leaderKey() const { return m_leaderKey; }
    void setLeaderKey(const QString &key);
    /// How long, in milliseconds, the leader waits for the next key; 0 waits
    /// until Escape.
    [[nodiscard]] int leaderTimeout() const { return m_leaderTimeout; }
    void setLeaderTimeout(int ms);
    static inline const QStringList kLeaderKeys = {QStringLiteral(","), QStringLiteral(":"),
                                                   QStringLiteral(" ")};

    /// Calendars the user hid in Callie, by CalendarInfo::id.
    [[nodiscard]] QStringList hiddenCalendars() const { return m_hiddenCalendars; }
    Q_INVOKABLE void setCalendarVisible(const QString &id, bool visible);

    /// Times in the chosen zone and clock format. A JS Date only knows the
    /// system zone, so QML formats and places times through this.
    [[nodiscard]] Times *times() const { return m_times; }
    /// Accounts whose calendars the sidebar folds away.
    [[nodiscard]] QStringList collapsedAccounts() const { return m_collapsedAccounts; }
    Q_INVOKABLE void setAccountCollapsed(const QString &account, bool collapsed);

    [[nodiscard]] QVariantMap calendarLooks() const { return m_calendarLooks; }
    /// An empty name or an invalid color goes back to the calendar's own.
    Q_INVOKABLE void setCalendarName(const QString &id, const QString &name);
    Q_INVOKABLE void setCalendarColor(const QString &id, const QColor &color);
    Q_INVOKABLE void resetCalendarLook(const QString &id);

    [[nodiscard]] QVariantMap accountNames() const { return m_accountNames; }
    [[nodiscard]] QVariantMap accountPhotos() const { return m_accountPhotos; }
    /// An empty photo forgets it, so the account shows its letter.
    void setAccountPhoto(const QString &account, const QUrl &photo);
    /// An empty name goes back to the account's own.
    Q_INVOKABLE void setAccountName(const QString &account, const QString &name);
    /// The user's name for `account`, or the account itself.
    Q_INVOKABLE QString accountName(const QString &account) const;

    [[nodiscard]] QString theme() const { return m_theme; }
    void setTheme(const QString &idOrPath);

    [[nodiscard]] QString newEventCalendar() const { return m_newEventCalendar; }
    void setNewEventCalendar(const QString &id);
    [[nodiscard]] QString defaultCalendar() const { return m_defaultCalendar; }
    void setDefaultCalendar(const QString &id);

    [[nodiscard]] QString lastSeenVersion() const { return m_lastSeenVersion; }
    void setLastSeenVersion(const QString &version);

    [[nodiscard]] QString view() const { return m_view; }
    void setView(const QString &view);

    [[nodiscard]] bool notify() const { return m_notify; }
    void setNotify(bool notify);

    [[nodiscard]] int reminderMinutes() const { return m_reminderMinutes; }
    void setReminderMinutes(int minutes);

    [[nodiscard]] bool keepRunning() const { return m_keepRunning; }
    void setKeepRunning(bool keep);

    /// Every IANA zone id, for the zone picker.
    Q_INVOKABLE static QStringList availableTimeZones();

    /// Forgets every preference.
    Q_INVOKABLE void reset();

    /// Takes the week start, weekend, clock and declined-event choices from
    /// Google Calendar's settings, for whichever of them the user has not set
    /// in Callie.
    void seedFromGoogle(const QHash<QString, QString> &google);

Q_SIGNALS:
    void timeFormatChanged();
    void timeZoneChanged();
    void showDeclinedChanged();
    void dimPastChanged();
    void widenTodayChanged();
    void weekChanged();
    void workHoursChanged();
    void keyboardChanged();
    void hiddenCalendarsChanged();
    void timesChanged();
    void themeChanged();
    void collapsedAccountsChanged();
    void calendarLooksChanged();
    void accountNamesChanged();
    void accountPhotosChanged();
    void viewChanged();
    void lastSeenVersionChanged();
    void newEventCalendarChanged();
    void defaultCalendarChanged();
    void notifyChanged();
    void reminderMinutesChanged();
    void keepRunningChanged();

private:
    void load();
    void rebuildTimes();

    QSettings m_store;
    TimeFormat m_timeFormat = TimeFormat::Locale;
    QString m_timeZoneId;
    bool m_showDeclined = true;
    bool m_dimPast = true;
    bool m_widenToday = false;
    int m_weekStart = 0;
    bool m_hideWeekends = false;
    bool m_weekNumbers = false;
    int m_workStart = 9 * 60;
    int m_workEnd = 17 * 60;
    bool m_showWorkHours = false;
    bool m_viMode = false;
    QString m_leaderKey = QStringLiteral(",");
    int m_leaderTimeout = 5000;
    QStringList m_hiddenCalendars;
    Times *m_times = nullptr;
    QString m_theme;
    QStringList m_collapsedAccounts;
    QVariantMap m_calendarLooks;
    QVariantMap m_accountNames;
    QVariantMap m_accountPhotos;
    QString m_view;
    QString m_lastSeenVersion;
    QString m_newEventCalendar;
    QString m_defaultCalendar;
    bool m_notify = true;
    int m_reminderMinutes = 10;
    bool m_keepRunning = false;
};

} // namespace callie
