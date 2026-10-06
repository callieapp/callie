#include "EventActions.h"

#include <QUrlQuery>

using namespace Qt::StringLiterals;

namespace callie {

Event EventActions::toEvent(const QVariantMap &event)
{
    Event e;
    e.calendarId = event.value(u"calendarId"_s).toString();
    e.eventId = event.value(u"eventId"_s).toString();
    e.seriesId = event.value(u"seriesId"_s).toString();
    e.summary = event.value(u"summary"_s).toString();
    e.allDay = event.value(u"allDay"_s).toBool();
    e.start = event.value(u"start"_s).toDateTime();
    e.end = event.value(u"end"_s).toDateTime();
    e.recurrenceId = event.value(u"recurrenceId"_s).toDateTime();
    return e;
}

void EventActions::respond(const QVariantMap &event, const QString &status, bool wholeSeries)
{
    if (!m_source || m_busy)
        return;
    start();
    const QPointer<EventActions> self(this);
    const QString eventId = event.value(u"eventId"_s).toString();
    m_source->respond(toEvent(event), status, wholeSeries,
                      [this, self, eventId, status](const QString &error) {
                          if (!self)
                              return;
                          finish(eventId, error);
                          if (error.isEmpty())
                              Q_EMIT responded(eventId, status);
                      });
}

void EventActions::remove(const QVariantMap &event, bool wholeSeries)
{
    if (!m_source || m_busy)
        return;
    start();
    const QPointer<EventActions> self(this);
    const QString eventId = event.value(u"eventId"_s).toString();
    m_source->deleteEvent(toEvent(event), wholeSeries, [this, self, eventId](const QString &error) {
        if (!self)
            return;
        finish(eventId, error);
        if (error.isEmpty())
            Q_EMIT removed(eventId);
    });
}

QUrl EventActions::mailGuests(const QVariantMap &event)
{
    const QStringList guests = event.value(u"attendees"_s).toStringList();
    if (guests.isEmpty())
        return {};
    QUrl url;
    url.setScheme(u"mailto"_s);
    url.setPath(guests.join(u','));
    QUrlQuery query;
    query.addQueryItem(u"subject"_s, QString::fromUtf8(QUrl::toPercentEncoding(
                                         event.value(u"summary"_s).toString())));
    url.setQuery(query.query(QUrl::FullyEncoded), QUrl::StrictMode);
    return url;
}

void EventActions::clearError()
{
    if (m_error.isEmpty())
        return;
    m_error.clear();
    m_errorEventId.clear();
    Q_EMIT errorChanged();
}

void EventActions::start()
{
    m_busy = true;
    Q_EMIT busyChanged();
    clearError();
}

void EventActions::finish(const QString &eventId, const QString &error)
{
    m_busy = false;
    Q_EMIT busyChanged();
    if (!error.isEmpty()) {
        m_error = error;
        m_errorEventId = eventId;
        Q_EMIT errorChanged();
    }
}

} // namespace callie
