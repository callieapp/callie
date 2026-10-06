#include "callie/Settings.h"

#include <QLocale>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

const QString kTimeFormat = u"time/format"_s;
const QString kTimeZone = u"time/zone"_s;
const QString kShowDeclined = u"events/showDeclined"_s;
const QString kDimPast = u"events/dimPast"_s;
const QString kWidenToday = u"week/widenToday"_s;
const QString kHiddenCalendars = u"calendars/hidden"_s;
const QString kCollapsedAccounts = u"calendars/collapsedAccounts"_s;
const QString kTheme = u"appearance/theme"_s;

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
    m_hiddenCalendars = m_store.value(kHiddenCalendars).toStringList();
    m_collapsedAccounts = m_store.value(kCollapsedAccounts).toStringList();
    m_theme = m_store.value(kTheme).toString();
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

void Settings::setWidenToday(bool widen)
{
    if (m_widenToday == widen)
        return;
    m_widenToday = widen;
    m_store.setValue(kWidenToday, widen);
    Q_EMIT widenTodayChanged();
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

QStringList Settings::availableTimeZones()
{
    QStringList ids;
    for (const QByteArray &id : QTimeZone::availableTimeZoneIds())
        ids.append(QString::fromUtf8(id));
    return ids;
}

void Settings::reset()
{
    m_store.clear();
    const TimeFormat format = m_timeFormat;
    const QString zone = m_timeZoneId;
    const bool declined = m_showDeclined, dim = m_dimPast, widen = m_widenToday;
    const QStringList hidden = m_hiddenCalendars;
    const QStringList collapsed = m_collapsedAccounts;
    const QString theme = m_theme;
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
    if (theme != m_theme)
        Q_EMIT themeChanged();
    rebuildTimes();
}

} // namespace callie
