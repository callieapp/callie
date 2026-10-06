#pragma once

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
    Q_PROPERTY(QTimeZone timeZone READ timeZone NOTIFY timeZoneChanged)
    Q_PROPERTY(bool showDeclined READ showDeclined WRITE setShowDeclined NOTIFY showDeclinedChanged)
    Q_PROPERTY(bool dimPast READ dimPast WRITE setDimPast NOTIFY dimPastChanged)
    Q_PROPERTY(bool widenToday READ widenToday WRITE setWidenToday NOTIFY widenTodayChanged)
    Q_PROPERTY(QStringList hiddenCalendars READ hiddenCalendars NOTIFY hiddenCalendarsChanged)

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

    /// A clock time in the chosen format: "14:30" or "2:30 PM".
    Q_INVOKABLE QString formatTime(const QDateTime &time) const;
    /// An hour label for the grid: "14:00" or "2 PM".
    Q_INVOKABLE QString formatHour(int hour) const;

    /// The calendar date of `time` in the chosen zone, which QML cannot work
    /// out from a Date in the system zone.
    Q_INVOKABLE QDate dateIn(const QDateTime &time) const;
    /// Minutes since midnight of `time` in the chosen zone.
    Q_INVOKABLE int minutesIntoDay(const QDateTime &time) const;

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

private:
    void load();

    QSettings m_store;
    TimeFormat m_timeFormat = TimeFormat::Locale;
    QString m_timeZoneId;
    bool m_showDeclined = true;
    bool m_dimPast = true;
    bool m_widenToday = false;
    QStringList m_hiddenCalendars;
};

} // namespace callie
