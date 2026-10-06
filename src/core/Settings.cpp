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

} // namespace

Settings::Settings(const QString &path, QObject *parent)
    : QObject(parent), m_store(path, QSettings::IniFormat)
{
    load();
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
    m_timeZoneId = m_store.value(kTimeZone).toString();
    m_showDeclined = m_store.value(kShowDeclined, true).toBool();
    m_dimPast = m_store.value(kDimPast, true).toBool();
    m_widenToday = m_store.value(kWidenToday, false).toBool();
    m_hiddenCalendars = m_store.value(kHiddenCalendars).toStringList();
}

void Settings::setTimeFormat(TimeFormat format)
{
    if (m_timeFormat == format)
        return;
    m_timeFormat = format;
    m_store.setValue(kTimeFormat, int(format));
    Q_EMIT timeFormatChanged();
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

QString Settings::formatTime(const QDateTime &time) const
{
    const QDateTime local = time.toTimeZone(timeZone());
    return use24Hour() ? local.toString(u"HH:mm"_s) : QLocale().toString(local, u"h:mm AP"_s);
}

QString Settings::formatHour(int hour) const
{
    const QTime time(hour % 24, 0);
    return use24Hour() ? time.toString(u"HH:mm"_s) : QLocale().toString(time, u"h AP"_s);
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
}

} // namespace callie
