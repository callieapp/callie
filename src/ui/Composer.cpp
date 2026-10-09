#include "Composer.h"

#include "Clock.h"
#include "EventModelForeign.h"

#include "callie/Settings.h"

using namespace Qt::StringLiterals;

namespace callie {

Composer::Composer(QObject *parent) : QObject(parent)
{
    connect(SettingsForeign::create(nullptr, nullptr), &Settings::hiddenCalendarsChanged, this,
            &Composer::refreshCalendars);
    reparse();
}

void Composer::setSource(CalendarSource *source)
{
    if (m_source == source)
        return;
    if (m_source)
        disconnect(m_source, nullptr, this, nullptr);
    m_source = source;
    if (m_source)
        connect(m_source, &CalendarSource::changed, this, &Composer::refreshCalendars);
    Q_EMIT sourceChanged();
    refreshCalendars();
}

void Composer::setText(const QString &text)
{
    if (m_text == text)
        return;
    m_text = text;
    setError({});
    reparse();
}

void Composer::reset()
{
    // Each new event starts in the chosen default, or the calendar used last.
    m_calendarId.clear();
    refreshCalendars();
    m_text.clear();
    m_pickedStart = {};
    m_pickedEnd = {};
    setError({});
    reparse();
}

void Composer::pickTimes(const QDateTime &start, const QDateTime &end)
{
    // From QML these arrive in the system zone; the event belongs in the chosen
    // one, which Google is then told.
    const QTimeZone zone = SettingsForeign::create(nullptr, nullptr)->timeZone();
    m_pickedStart = start.isValid() ? start.toTimeZone(zone) : QDateTime();
    m_pickedEnd = end.isValid() ? end.toTimeZone(zone) : QDateTime();
    reparse();
}

void Composer::reparse()
{
    Settings *settings = SettingsForeign::create(nullptr, nullptr);
    m_draft = QuickAdd::parse(m_text, Clock::instance()->now(), settings->timeZone());
    if (m_draft.timeGuessed && m_pickedStart.isValid()) {
        m_draft.start = m_pickedStart;
        m_draft.end = m_draft.lengthMinutes > 0
                          ? m_pickedStart.addSecs(qint64(m_draft.lengthMinutes) * 60)
                          : m_pickedEnd;
    }
    Q_EMIT draftChanged();
}

void Composer::refreshCalendars()
{
    QVariantList calendars;
    QStringList ids;
    if (m_source) {
        const QStringList hidden = SettingsForeign::create(nullptr, nullptr)->hiddenCalendars();
        for (const CalendarInfo &calendar : m_source->calendars()) {
            // A new event in a calendar that is not shown would seem to vanish.
            if (!calendar.writable || hidden.contains(calendar.id))
                continue;
            ids << calendar.id;
            calendars << QVariantMap{{u"id"_s, calendar.id},
                                     {u"name"_s, calendar.displayName},
                                     {u"color"_s, calendar.color}};
        }
    }
    if (calendars != m_calendars) {
        m_calendars = calendars;
        Q_EMIT calendarsChanged();
    }
    if (ids.contains(m_calendarId))
        return;
    const Settings *settings = SettingsForeign::create(nullptr, nullptr);
    const QString chosen = settings->defaultCalendar();
    const QString last = settings->newEventCalendar();
    m_calendarId = ids.contains(chosen) ? chosen : ids.contains(last) ? last : ids.value(0);
    Q_EMIT calendarIdChanged();
    Q_EMIT draftChanged();
}

void Composer::setCalendarId(const QString &id)
{
    if (m_calendarId == id)
        return;
    m_calendarId = id;
    Q_EMIT calendarIdChanged();
    Q_EMIT draftChanged();
}

bool Composer::ready() const
{
    return m_draft.isValid() && !m_calendarId.isEmpty();
}

void Composer::submit()
{
    if (!ready() || m_busy || !m_source)
        return;
    EventDraft draft = m_draft;
    draft.calendarId = m_calendarId;
    m_busy = true;
    Q_EMIT busyChanged();
    setError({});
    SettingsForeign::create(nullptr, nullptr)->setNewEventCalendar(m_calendarId);
    const QPointer<Composer> self(this);
    m_source->createEvent(draft, [this, self](const QString &error) {
        if (!self)
            return;
        m_busy = false;
        Q_EMIT busyChanged();
        if (!error.isEmpty()) {
            setError(error);
            return;
        }
        m_text.clear();
        reparse();
        Q_EMIT created();
    });
}

void Composer::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    Q_EMIT errorChanged();
}

} // namespace callie
