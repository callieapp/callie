#include "callie/Settings.h"

#include <QLocale>
#include <QStandardPaths>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

const QString kTimeFormat = u"time/format"_s;
const QString kTimeZone = u"time/zone"_s;
const QString kShowDeclined = u"events/showDeclined"_s;
const QString kDimPast = u"events/dimPast"_s;
const QString kWidenToday = u"week/widenToday"_s;
const QString kWeekStart = u"week/start"_s;
const QString kHideWeekends = u"week/hideWeekends"_s;
const QString kWeekNumbers = u"week/numbers"_s;
const QString kWorkStart = u"week/workStart"_s;
const QString kWorkEnd = u"week/workEnd"_s;
constexpr int kDayMinutes = 24 * 60;
const QString kHiddenCalendars = u"calendars/hidden"_s;
const QString kCollapsedAccounts = u"calendars/collapsedAccounts"_s;
const QString kCalendarLooks = u"calendars/looks"_s;
const QString kAccountNames = u"calendars/accountNames"_s;
const QString kTheme = u"appearance/theme"_s;
const QString kView = u"view/current"_s;
const QString kLastSeenVersion = u"app/lastSeenVersion"_s;
const QString kNewEventCalendar = u"events/newEventCalendar"_s;
const QString kDefaultCalendar = u"events/defaultCalendar"_s;
const QString kNotify = u"reminders/notify"_s;
const QString kReminderMinutes = u"reminders/defaultMinutes"_s;
const QString kKeepRunning = u"reminders/keepRunning"_s;
const QStringList kViews{u"day"_s, u"week"_s, u"month"_s, u"agenda"_s};

} // namespace

Settings::Settings(const QString &path, QObject *parent)
    : QObject(parent), m_store(path, QSettings::IniFormat)
{
    load();
    rebuildTimes();
}

QString Settings::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           u"/callie/settings.ini"_s;
}

void Settings::load()
{
    const int format = m_store.value(kTimeFormat, 0).toInt();
    m_timeFormat = format >= 0 && format <= int(TimeFormat::TwelveHour) ? TimeFormat(format)
                                                                        : TimeFormat::Locale;
    // A hand edit or a tzdata update can leave an id this system lacks.
    const QString zone = m_store.value(kTimeZone).toString();
    m_timeZoneId = QTimeZone::isTimeZoneIdAvailable(zone.toUtf8()) ? zone : QString();
    m_showDeclined = m_store.value(kShowDeclined, true).toBool();
    m_dimPast = m_store.value(kDimPast, true).toBool();
    m_widenToday = m_store.value(kWidenToday, false).toBool();
    m_weekStart = std::clamp(m_store.value(kWeekStart, 0).toInt(), 0, 7);
    m_hideWeekends = m_store.value(kHideWeekends, false).toBool();
    m_weekNumbers = m_store.value(kWeekNumbers, false).toBool();
    m_workStart = std::clamp(m_store.value(kWorkStart, 9 * 60).toInt(), 0, kDayMinutes - 30);
    m_workEnd = std::clamp(m_store.value(kWorkEnd, 17 * 60).toInt(), m_workStart + 30, kDayMinutes);
    m_hiddenCalendars = m_store.value(kHiddenCalendars).toStringList();
    m_collapsedAccounts = m_store.value(kCollapsedAccounts).toStringList();
    m_calendarLooks = m_store.value(kCalendarLooks).toMap();
    m_accountNames = m_store.value(kAccountNames).toMap();
    m_theme = m_store.value(kTheme).toString();
    const QString view = m_store.value(kView).toString();
    m_view = kViews.contains(view) ? view : u"week"_s;
    m_lastSeenVersion = m_store.value(kLastSeenVersion).toString();
    m_newEventCalendar = m_store.value(kNewEventCalendar).toString();
    m_defaultCalendar = m_store.value(kDefaultCalendar).toString();
    m_notify = m_store.value(kNotify, true).toBool();
    m_reminderMinutes = std::max(-1, m_store.value(kReminderMinutes, 10).toInt());
    m_keepRunning = m_store.value(kKeepRunning, false).toBool();
}

void Settings::setTimeFormat(TimeFormat format)
{
    if (m_timeFormat == format)
        return;
    m_timeFormat = format;
    m_store.setValue(kTimeFormat, int(format));
    Q_EMIT timeFormatChanged();
    rebuildTimes();
}

bool Settings::use24Hour() const
{
    switch (m_timeFormat) {
    case TimeFormat::TwentyFourHour: return true;
    case TimeFormat::TwelveHour: return false;
    case TimeFormat::Locale: break;
    }
    // Locales with a 12-hour clock put an AM/PM marker in their time format.
    return !QLocale().timeFormat(QLocale::ShortFormat).contains(u'a', Qt::CaseInsensitive);
}

void Settings::setTimeZoneId(const QString &id)
{
    // An unknown id would silently show UTC, so it means the system zone.
    const QString kept = QTimeZone::isTimeZoneIdAvailable(id.toUtf8()) ? id : QString();
    if (m_timeZoneId == kept)
        return;
    m_timeZoneId = kept;
    m_store.setValue(kTimeZone, kept);
    Q_EMIT timeZoneChanged();
    rebuildTimes();
}

QTimeZone Settings::timeZone() const
{
    return m_timeZoneId.isEmpty() ? QTimeZone::systemTimeZone() : QTimeZone(m_timeZoneId.toUtf8());
}

void Settings::setShowDeclined(bool show)
{
    if (m_showDeclined == show)
        return;
    m_showDeclined = show;
    m_store.setValue(kShowDeclined, show);
    Q_EMIT showDeclinedChanged();
}

void Settings::setDimPast(bool dim)
{
    if (m_dimPast == dim)
        return;
    m_dimPast = dim;
    m_store.setValue(kDimPast, dim);
    Q_EMIT dimPastChanged();
}

void Settings::setWeekStart(int day)
{
    day = std::clamp(day, 0, 7);
    if (m_weekStart == day)
        return;
    m_weekStart = day;
    m_store.setValue(kWeekStart, day);
    Q_EMIT weekChanged();
}

int Settings::firstDayOfWeek() const
{
    return m_weekStart == 0 ? int(QLocale().firstDayOfWeek()) : m_weekStart;
}

void Settings::setHideWeekends(bool hide)
{
    if (m_hideWeekends == hide)
        return;
    m_hideWeekends = hide;
    m_store.setValue(kHideWeekends, hide);
    Q_EMIT weekChanged();
}

void Settings::setWeekNumbers(bool show)
{
    if (m_weekNumbers == show)
        return;
    m_weekNumbers = show;
    m_store.setValue(kWeekNumbers, show);
    Q_EMIT weekChanged();
}

void Settings::setWorkStart(int minutes)
{
    // Working hours are at least half an hour, so the end moves along if needed.
    minutes = std::clamp(minutes, 0, kDayMinutes - 30);
    if (m_workStart == minutes)
        return;
    m_workStart = minutes;
    m_store.setValue(kWorkStart, minutes);
    if (m_workEnd < minutes + 30) {
        m_workEnd = minutes + 30;
        m_store.setValue(kWorkEnd, m_workEnd);
    }
    Q_EMIT workHoursChanged();
}

void Settings::setWorkEnd(int minutes)
{
    minutes = std::clamp(minutes, 30, kDayMinutes);
    if (m_workEnd == minutes)
        return;
    m_workEnd = minutes;
    m_store.setValue(kWorkEnd, minutes);
    if (m_workStart > minutes - 30) {
        m_workStart = minutes - 30;
        m_store.setValue(kWorkStart, m_workStart);
    }
    Q_EMIT workHoursChanged();
}

void Settings::setWidenToday(bool widen)
{
    if (m_widenToday == widen)
        return;
    m_widenToday = widen;
    m_store.setValue(kWidenToday, widen);
    Q_EMIT widenTodayChanged();
}

void Settings::setNewEventCalendar(const QString &id)
{
    if (m_newEventCalendar == id)
        return;
    m_newEventCalendar = id;
    m_store.setValue(kNewEventCalendar, id);
    Q_EMIT newEventCalendarChanged();
}

void Settings::setDefaultCalendar(const QString &id)
{
    if (m_defaultCalendar == id)
        return;
    m_defaultCalendar = id;
    m_store.setValue(kDefaultCalendar, id);
    Q_EMIT defaultCalendarChanged();
}

void Settings::setLastSeenVersion(const QString &version)
{
    if (m_lastSeenVersion == version)
        return;
    m_lastSeenVersion = version;
    m_store.setValue(kLastSeenVersion, version);
    Q_EMIT lastSeenVersionChanged();
}

void Settings::setView(const QString &view)
{
    if (m_view == view || !kViews.contains(view))
        return;
    m_view = view;
    m_store.setValue(kView, view);
    Q_EMIT viewChanged();
}

void Settings::setCalendarVisible(const QString &id, bool visible)
{
    if (m_hiddenCalendars.contains(id) == !visible)
        return;
    if (visible)
        m_hiddenCalendars.removeAll(id);
    else
        m_hiddenCalendars.append(id);
    m_store.setValue(kHiddenCalendars, m_hiddenCalendars);
    Q_EMIT hiddenCalendarsChanged();
}

void Settings::setTheme(const QString &idOrPath)
{
    if (m_theme == idOrPath)
        return;
    m_theme = idOrPath;
    m_store.setValue(kTheme, idOrPath);
    Q_EMIT themeChanged();
}

void Settings::setAccountCollapsed(const QString &account, bool collapsed)
{
    if (m_collapsedAccounts.contains(account) == collapsed)
        return;
    if (collapsed)
        m_collapsedAccounts.append(account);
    else
        m_collapsedAccounts.removeAll(account);
    m_store.setValue(kCollapsedAccounts, m_collapsedAccounts);
    Q_EMIT collapsedAccountsChanged();
}

void Settings::rebuildTimes()
{
    if (m_times && m_times->zone() == timeZone() && m_times->use24Hour() == use24Hour())
        return;
    // A new object, not a mutated one, so bindings holding the old one re-run.
    if (m_times)
        m_times->deleteLater();
    m_times = new Times(timeZone(), use24Hour(), this);
    Q_EMIT timesChanged();
}

void Settings::setNotify(bool notify)
{
    if (m_notify == notify)
        return;
    m_notify = notify;
    m_store.setValue(kNotify, notify);
    Q_EMIT notifyChanged();
}

void Settings::setReminderMinutes(int minutes)
{
    minutes = std::max(-1, minutes);
    if (m_reminderMinutes == minutes)
        return;
    m_reminderMinutes = minutes;
    m_store.setValue(kReminderMinutes, minutes);
    Q_EMIT reminderMinutesChanged();
}

void Settings::setKeepRunning(bool keep)
{
    if (m_keepRunning == keep)
        return;
    m_keepRunning = keep;
    m_store.setValue(kKeepRunning, keep);
    Q_EMIT keepRunningChanged();
}

QStringList Settings::availableTimeZones()
{
    QStringList ids;
    for (const QByteArray &id : QTimeZone::availableTimeZoneIds())
        ids.append(QString::fromUtf8(id));
    return ids;
}

void Settings::seedFromGoogle(const QHash<QString, QString> &google)
{
    const auto unset = [this, &google](const QString &ours, const QString &theirs) {
        return !m_store.contains(ours) && google.contains(theirs);
    };
    const auto yes = [&google](const QString &id) { return google.value(id) == u"true"; };
    // Each is stored even when it matches Callie's default, so it counts as
    // set and a second account leaves it alone.
    if (unset(kWeekStart, u"weekStart"_s)) {
        // Google counts from Sunday as 0; Qt from Monday as 1.
        const int day = google.value(u"weekStart"_s).toInt();
        setWeekStart(day == 0 ? int(Qt::Sunday) : std::clamp(day, 1, 6));
        m_store.setValue(kWeekStart, m_weekStart);
    }
    if (unset(kHideWeekends, u"hideWeekends"_s)) {
        setHideWeekends(yes(u"hideWeekends"_s));
        m_store.setValue(kHideWeekends, m_hideWeekends);
    }
    if (unset(kTimeFormat, u"format24HourTime"_s)) {
        setTimeFormat(yes(u"format24HourTime"_s) ? TimeFormat::TwentyFourHour
                                                 : TimeFormat::TwelveHour);
        m_store.setValue(kTimeFormat, int(m_timeFormat));
    }
    if (unset(kShowDeclined, u"showDeclinedEvents"_s)) {
        setShowDeclined(yes(u"showDeclinedEvents"_s));
        m_store.setValue(kShowDeclined, m_showDeclined);
    }
}

void Settings::setCalendarName(const QString &id, const QString &name)
{
    QVariantMap look = m_calendarLooks.value(id).toMap();
    const QString trimmed = name.trimmed();
    if (look.value(u"name"_s).toString() == trimmed)
        return;
    if (trimmed.isEmpty())
        look.remove(u"name"_s);
    else
        look.insert(u"name"_s, trimmed);
    if (look.isEmpty())
        m_calendarLooks.remove(id);
    else
        m_calendarLooks.insert(id, look);
    m_store.setValue(kCalendarLooks, m_calendarLooks);
    Q_EMIT calendarLooksChanged();
}

void Settings::setCalendarColor(const QString &id, const QColor &color)
{
    QVariantMap look = m_calendarLooks.value(id).toMap();
    const QString name = color.isValid() ? color.name() : QString();
    if (look.value(u"color"_s).toString() == name)
        return;
    if (name.isEmpty())
        look.remove(u"color"_s);
    else
        look.insert(u"color"_s, name);
    if (look.isEmpty())
        m_calendarLooks.remove(id);
    else
        m_calendarLooks.insert(id, look);
    m_store.setValue(kCalendarLooks, m_calendarLooks);
    Q_EMIT calendarLooksChanged();
}

void Settings::resetCalendarLook(const QString &id)
{
    if (!m_calendarLooks.remove(id))
        return;
    m_store.setValue(kCalendarLooks, m_calendarLooks);
    Q_EMIT calendarLooksChanged();
}

void Settings::setAccountName(const QString &account, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (m_accountNames.value(account).toString() == trimmed)
        return;
    if (trimmed.isEmpty())
        m_accountNames.remove(account);
    else
        m_accountNames.insert(account, trimmed);
    m_store.setValue(kAccountNames, m_accountNames);
    Q_EMIT accountNamesChanged();
}

QString Settings::accountName(const QString &account) const
{
    return m_accountNames.value(account, account).toString();
}

void Settings::reset()
{
    m_store.clear();
    const TimeFormat format = m_timeFormat;
    const QString zone = m_timeZoneId;
    const bool declined = m_showDeclined, dim = m_dimPast, widen = m_widenToday;
    const QStringList hidden = m_hiddenCalendars;
    const QStringList collapsed = m_collapsedAccounts;
    const QVariantMap looks = m_calendarLooks, accountNames = m_accountNames;
    const QString theme = m_theme;
    const QString view = m_view;
    const QString defaultCalendar = m_defaultCalendar;
    const bool notify = m_notify, keep = m_keepRunning;
    const int minutes = m_reminderMinutes;
    const int weekStart = m_weekStart, workStart = m_workStart, workEnd = m_workEnd;
    const bool hideWeekends = m_hideWeekends, weekNumbers = m_weekNumbers;
    load();
    if (format != m_timeFormat)
        Q_EMIT timeFormatChanged();
    if (zone != m_timeZoneId)
        Q_EMIT timeZoneChanged();
    if (declined != m_showDeclined)
        Q_EMIT showDeclinedChanged();
    if (dim != m_dimPast)
        Q_EMIT dimPastChanged();
    if (widen != m_widenToday)
        Q_EMIT widenTodayChanged();
    if (hidden != m_hiddenCalendars)
        Q_EMIT hiddenCalendarsChanged();
    if (collapsed != m_collapsedAccounts)
        Q_EMIT collapsedAccountsChanged();
    if (looks != m_calendarLooks)
        Q_EMIT calendarLooksChanged();
    if (accountNames != m_accountNames)
        Q_EMIT accountNamesChanged();
    if (theme != m_theme)
        Q_EMIT themeChanged();
    if (view != m_view)
        Q_EMIT viewChanged();
    if (defaultCalendar != m_defaultCalendar)
        Q_EMIT defaultCalendarChanged();
    if (notify != m_notify)
        Q_EMIT notifyChanged();
    if (minutes != m_reminderMinutes)
        Q_EMIT reminderMinutesChanged();
    if (keep != m_keepRunning)
        Q_EMIT keepRunningChanged();
    if (weekStart != m_weekStart || hideWeekends != m_hideWeekends || weekNumbers != m_weekNumbers)
        Q_EMIT weekChanged();
    if (workStart != m_workStart || workEnd != m_workEnd)
        Q_EMIT workHoursChanged();
    rebuildTimes();
}

} // namespace callie
