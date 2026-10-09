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
const QString kShowWorkHours = u"week/showWorkHours"_s;
const QString kDeveloperMode = u"developer/mode"_s;
const QString kMapApp = u"general/mapApp"_s;
const QString kSearchPlaces = u"general/searchPlacesOnline"_s;
const QStringList kMapApps = {u"system"_s, u"google"_s, u"osm"_s, u"apple"_s};
const QString kVerboseLogging = u"developer/verboseLogging"_s;
const QString kViMode = u"keyboard/viMode"_s;
const QString kLeaderKey = u"keyboard/leaderKey"_s;
const QString kLeaderTimeout = u"keyboard/leaderTimeout"_s;
constexpr int kDayMinutes = 24 * 60;
const QString kHiddenCalendars = u"calendars/hidden"_s;
const QString kKnownCalendars = u"calendars/known"_s;
const QString kCollapsedAccounts = u"calendars/collapsedAccounts"_s;
const QString kCalendarLooks = u"calendars/looks"_s;
const QString kAccountNames = u"calendars/accountNames"_s;
const QString kAccountPhotos = u"calendars/accountPhotos"_s;
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
    m_showWorkHours = m_store.value(kShowWorkHours, false).toBool();
    m_developerMode = m_store.value(kDeveloperMode, false).toBool();
    const QString mapApp = m_store.value(kMapApp).toString();
    m_mapApp = kMapApps.contains(mapApp) ? mapApp : u"system"_s;
    m_searchPlacesOnline = m_store.value(kSearchPlaces, true).toBool();
    m_verboseLogging = m_store.value(kVerboseLogging, false).toBool();
    m_viMode = m_store.value(kViMode, false).toBool();
    const QString leader = m_store.value(kLeaderKey).toString();
    m_leaderKey = kLeaderKeys.contains(leader) ? leader : u","_s;
    m_leaderTimeout = std::max(0, m_store.value(kLeaderTimeout, 5000).toInt());
    m_hiddenCalendars = m_store.value(kHiddenCalendars).toStringList();
    m_knownCalendars = m_store.value(kKnownCalendars).toStringList();
    m_collapsedAccounts = m_store.value(kCollapsedAccounts).toStringList();
    m_calendarLooks = m_store.value(kCalendarLooks).toMap();
    m_accountNames = m_store.value(kAccountNames).toMap();
    m_accountPhotos = m_store.value(kAccountPhotos).toMap();
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

void Settings::setViMode(bool on)
{
    if (m_viMode == on)
        return;
    m_viMode = on;
    m_store.setValue(kViMode, on);
    Q_EMIT keyboardChanged();
}

void Settings::setLeaderKey(const QString &key)
{
    if (m_leaderKey == key || !kLeaderKeys.contains(key))
        return;
    m_leaderKey = key;
    m_store.setValue(kLeaderKey, key);
    Q_EMIT keyboardChanged();
}

void Settings::setLeaderTimeout(int ms)
{
    ms = std::max(0, ms);
    if (m_leaderTimeout == ms)
        return;
    m_leaderTimeout = ms;
    m_store.setValue(kLeaderTimeout, ms);
    Q_EMIT keyboardChanged();
}

void Settings::setMapApp(const QString &app)
{
    if (m_mapApp == app || !kMapApps.contains(app))
        return;
    m_mapApp = app;
    m_store.setValue(kMapApp, app);
    Q_EMIT placesChanged();
}

void Settings::setSearchPlacesOnline(bool on)
{
    if (m_searchPlacesOnline == on)
        return;
    m_searchPlacesOnline = on;
    m_store.setValue(kSearchPlaces, on);
    Q_EMIT placesChanged();
}

void Settings::setDeveloperMode(bool on)
{
    if (m_developerMode == on)
        return;
    m_developerMode = on;
    m_store.setValue(kDeveloperMode, on);
    Q_EMIT developerChanged();
}

void Settings::setVerboseLogging(bool on)
{
    if (m_verboseLogging == on)
        return;
    m_verboseLogging = on;
    m_store.setValue(kVerboseLogging, on);
    Q_EMIT developerChanged();
}

void Settings::setShowWorkHours(bool show)
{
    if (m_showWorkHours == show)
        return;
    m_showWorkHours = show;
    m_store.setValue(kShowWorkHours, show);
    Q_EMIT workHoursChanged();
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
    // A choice made counts as having seen the calendar, so seeding leaves it.
    if (!m_knownCalendars.contains(id)) {
        m_knownCalendars.append(id);
        m_store.setValue(kKnownCalendars, m_knownCalendars);
    }
    if (m_hiddenCalendars.contains(id) == !visible)
        return;
    if (visible)
        m_hiddenCalendars.removeAll(id);
    else
        m_hiddenCalendars.append(id);
    m_store.setValue(kHiddenCalendars, m_hiddenCalendars);
    Q_EMIT hiddenCalendarsChanged();
}

void Settings::seedCalendars(const QList<CalendarInfo> &calendars)
{
    bool hidden = false;
    bool seen = false;
    for (const CalendarInfo &calendar : calendars) {
        if (m_knownCalendars.contains(calendar.id))
            continue;
        m_knownCalendars.append(calendar.id);
        seen = true;
        if (!calendar.enabled && !m_hiddenCalendars.contains(calendar.id)) {
            m_hiddenCalendars.append(calendar.id);
            hidden = true;
        }
    }
    if (seen)
        m_store.setValue(kKnownCalendars, m_knownCalendars);
    if (hidden) {
        m_store.setValue(kHiddenCalendars, m_hiddenCalendars);
        Q_EMIT hiddenCalendarsChanged();
    }
}

bool Settings::isShown(const CalendarInfo &calendar) const
{
    if (m_hiddenCalendars.contains(calendar.id))
        return false;
    return m_knownCalendars.contains(calendar.id) || calendar.enabled;
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

void Settings::setAccountPhoto(const QString &account, const QUrl &photo)
{
    if (m_accountPhotos.value(account).toUrl() == photo)
        return;
    if (photo.isEmpty())
        m_accountPhotos.remove(account);
    else
        m_accountPhotos.insert(account, photo);
    m_store.setValue(kAccountPhotos, m_accountPhotos);
    Q_EMIT accountPhotosChanged();
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
    const QStringList hidden = m_hiddenCalendars, known = m_knownCalendars;
    const QStringList collapsed = m_collapsedAccounts;
    const QVariantMap looks = m_calendarLooks, accountNames = m_accountNames;
    const QVariantMap accountPhotos = m_accountPhotos;
    const QString theme = m_theme;
    const QString view = m_view;
    const QString defaultCalendar = m_defaultCalendar;
    const bool notify = m_notify, keep = m_keepRunning;
    const int minutes = m_reminderMinutes;
    const int weekStart = m_weekStart, workStart = m_workStart, workEnd = m_workEnd;
    const bool showWorkHours = m_showWorkHours;
    const bool developerMode = m_developerMode, verboseLogging = m_verboseLogging;
    const QString mapApp = m_mapApp;
    const bool searchPlacesOnline = m_searchPlacesOnline;
    const bool hideWeekends = m_hideWeekends, weekNumbers = m_weekNumbers;
    const bool viMode = m_viMode;
    const QString leaderKey = m_leaderKey;
    const int leaderTimeout = m_leaderTimeout;
    load();
    // Photos are what Google says, not a choice, so they stay.
    m_accountPhotos = accountPhotos;
    if (!m_accountPhotos.isEmpty())
        m_store.setValue(kAccountPhotos, m_accountPhotos);
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
    // Calendars no longer remembered as seen are seeded again, by whoever listens.
    if (hidden != m_hiddenCalendars || known != m_knownCalendars)
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
    if (workStart != m_workStart || workEnd != m_workEnd || showWorkHours != m_showWorkHours)
        Q_EMIT workHoursChanged();
    if (mapApp != m_mapApp || searchPlacesOnline != m_searchPlacesOnline)
        Q_EMIT placesChanged();
    if (developerMode != m_developerMode || verboseLogging != m_verboseLogging)
        Q_EMIT developerChanged();
    if (viMode != m_viMode || leaderKey != m_leaderKey || leaderTimeout != m_leaderTimeout)
        Q_EMIT keyboardChanged();
    rebuildTimes();
}

} // namespace callie
