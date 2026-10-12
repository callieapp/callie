#include "callie/CombinedSource.h"

#include <algorithm>

namespace callie {

void CombinedSource::setSources(const QList<CalendarSource *> &sources)
{
    for (const QPointer<CalendarSource> &source : std::as_const(m_sources)) {
        if (source)
            disconnect(source, nullptr, this, nullptr);
    }
    m_sources.clear();
    for (CalendarSource *source : sources) {
        m_sources.append(source);
        connect(source, &CalendarSource::changed, this, &CalendarSource::changed);
        connect(source, &CalendarSource::statusChanged, this, &CalendarSource::statusChanged);
        connect(source, &CalendarSource::errorOccurred, this, &CalendarSource::errorOccurred);
        connect(source, &CalendarSource::changeMade, this, &CalendarSource::changeMade);
    }
    Q_EMIT changed();
    Q_EMIT statusChanged();
}

QList<CalendarSource *> CombinedSource::sources() const
{
    QList<CalendarSource *> live;
    for (const QPointer<CalendarSource> &source : m_sources) {
        if (source)
            live.append(source);
    }
    return live;
}

QList<CalendarInfo> CombinedSource::calendars() const
{
    QList<CalendarInfo> all;
    for (CalendarSource *source : sources())
        all += source->calendars();
    return all;
}

QList<Event> CombinedSource::eventsBetween(const QDateTime &from, const QDateTime &to,
                                           const QTimeZone &tz) const
{
    QList<Event> all;
    for (CalendarSource *source : sources())
        all += source->eventsBetween(from, to, tz);
    return all;
}

QFuture<SourceSnapshot> CombinedSource::load(const QDateTime &from, const QDateTime &to,
                                             const QTimeZone &tz) const
{
    QList<QFuture<SourceSnapshot>> loads;
    for (CalendarSource *source : sources())
        loads.append(source->load(from, to, tz));
    if (loads.isEmpty())
        return QtFuture::makeReadyValueFuture(SourceSnapshot{});
    if (loads.size() == 1)
        return loads.first();
    return QtFuture::whenAll(loads.begin(), loads.end())
        .then([](const QList<QFuture<SourceSnapshot>> &done) {
            SourceSnapshot all;
            for (const QFuture<SourceSnapshot> &load : done) {
                const SourceSnapshot part = load.result();
                all.events += part.events;
                all.calendars += part.calendars;
            }
            return all;
        });
}

void CombinedSource::refresh()
{
    for (CalendarSource *source : sources())
        source->refresh();
}

CalendarSource *CombinedSource::owner(const QString &calendarId, const Created &done) const
{
    for (CalendarSource *source : sources()) {
        for (const CalendarInfo &calendar : source->calendars()) {
            if (calendar.id == calendarId)
                return source;
        }
    }
    done(tr("That calendar is not connected any more."));
    return nullptr;
}

void CombinedSource::createEvent(const EventDraft &draft, Created done)
{
    if (CalendarSource *source = owner(draft.calendarId, done))
        source->createEvent(draft, std::move(done));
}

void CombinedSource::respond(const Event &event, const QString &status, bool wholeSeries,
                             Created done)
{
    if (CalendarSource *source = owner(event.calendarId, done))
        source->respond(event, status, wholeSeries, std::move(done));
}

void CombinedSource::deleteEvent(const Event &event, bool wholeSeries, Created done)
{
    if (CalendarSource *source = owner(event.calendarId, done))
        source->deleteEvent(event, wholeSeries, std::move(done));
}

void CombinedSource::moveEvent(const Event &event, const QDateTime &start, const QDateTime &end,
                               bool wholeSeries, Created done)
{
    if (CalendarSource *source = owner(event.calendarId, done))
        source->moveEvent(event, start, end, wholeSeries, std::move(done));
}

void CombinedSource::updateEvent(const Event &event, const EventEdit &edit, EditScope scope,
                                 Created done)
{
    if (CalendarSource *source = owner(event.calendarId, done))
        source->updateEvent(event, edit, scope, std::move(done));
}

bool CombinedSource::syncing() const
{
    const QList<CalendarSource *> all = sources();
    return std::ranges::any_of(all, [](const CalendarSource *source) { return source->syncing(); });
}

QDateTime CombinedSource::lastSynced() const
{
    QDateTime oldest;
    for (CalendarSource *source : sources()) {
        const QDateTime synced = source->lastSynced();
        if (synced.isValid() && (!oldest.isValid() || synced < oldest))
            oldest = synced;
    }
    return oldest;
}

QString CombinedSource::lastError() const
{
    for (CalendarSource *source : sources()) {
        if (const QString error = source->lastError(); !error.isEmpty())
            return error;
    }
    return {};
}

QVariantList CombinedSource::syncReport() const
{
    QVariantList all;
    for (CalendarSource *source : sources())
        all += source->syncReport();
    return all;
}

QStringList CombinedSource::waitingChanges() const
{
    QStringList all;
    for (CalendarSource *source : sources())
        all += source->waitingChanges();
    return all;
}

bool CombinedSource::undoChange(const QString &id)
{
    const QList<CalendarSource *> all = sources();
    return std::ranges::any_of(all,
                               [&id](CalendarSource *source) { return source->undoChange(id); });
}

} // namespace callie
