#include "callie/LookedSource.h"

#include "callie/Settings.h"

using namespace Qt::StringLiterals;

namespace callie {

LookedSource::LookedSource(CalendarSource &inner, Settings &settings, QObject *parent)
    : CalendarSource(parent), m_inner(&inner), m_settings(&settings)
{
    connect(m_inner, &CalendarSource::changed, this, &CalendarSource::changed);
    connect(m_inner, &CalendarSource::statusChanged, this, &CalendarSource::statusChanged);
    connect(m_inner, &CalendarSource::errorOccurred, this, &CalendarSource::errorOccurred);
    connect(m_settings, &Settings::calendarLooksChanged, this, &CalendarSource::changed);
}

void LookedSource::applyLooks(SourceSnapshot &snapshot, const QVariantMap &looks)
{
    if (looks.isEmpty())
        return;
    for (CalendarInfo &calendar : snapshot.calendars) {
        const QVariantMap look = looks.value(calendar.id).toMap();
        if (const QString name = look.value(u"name"_s).toString(); !name.isEmpty())
            calendar.displayName = name;
        if (const QColor color = QColor::fromString(look.value(u"color"_s).toString());
            color.isValid())
            calendar.color = color;
    }
    for (Event &event : snapshot.events) {
        const QColor color =
            QColor::fromString(looks.value(event.calendarId).toMap().value(u"color"_s).toString());
        if (color.isValid())
            event.color = color;
    }
}

QList<CalendarInfo> LookedSource::calendars() const
{
    SourceSnapshot snapshot{{}, m_inner->calendars()};
    applyLooks(snapshot, m_settings->calendarLooks());
    return snapshot.calendars;
}

QList<Event> LookedSource::eventsBetween(const QDateTime &from, const QDateTime &to,
                                         const QTimeZone &tz) const
{
    SourceSnapshot snapshot{m_inner->eventsBetween(from, to, tz), {}};
    applyLooks(snapshot, m_settings->calendarLooks());
    return snapshot.events;
}

QFuture<SourceSnapshot> LookedSource::load(const QDateTime &from, const QDateTime &to,
                                           const QTimeZone &tz) const
{
    // A copy, since the snapshot may finish on another thread.
    return m_inner->load(from, to, tz)
        .then([looks = m_settings->calendarLooks()](SourceSnapshot snapshot) {
            applyLooks(snapshot, looks);
            return snapshot;
        });
}

void LookedSource::createEvent(const EventDraft &draft, Created done)
{
    m_inner->createEvent(draft, std::move(done));
}

void LookedSource::respond(const Event &event, const QString &status, bool wholeSeries,
                           Created done)
{
    m_inner->respond(event, status, wholeSeries, std::move(done));
}

void LookedSource::deleteEvent(const Event &event, bool wholeSeries, Created done)
{
    m_inner->deleteEvent(event, wholeSeries, std::move(done));
}

void LookedSource::moveEvent(const Event &event, const QDateTime &start, const QDateTime &end,
                             bool wholeSeries, Created done)
{
    m_inner->moveEvent(event, start, end, wholeSeries, std::move(done));
}

} // namespace callie
