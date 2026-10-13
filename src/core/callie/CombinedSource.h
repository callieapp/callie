#pragma once

#include "CalendarSource.h"

#include <QList>
#include <QPointer>

namespace callie {

/// Several sources shown as one, such as Google accounts beside calendars on
/// this computer. Reads take every source's calendars and events; a change
/// goes to the source that holds the event's calendar.
class CombinedSource : public CalendarSource
{
    Q_OBJECT

public:
    using CalendarSource::CalendarSource;

    /// The sources, in the order their calendars are listed. Not owned.
    void setSources(const QList<CalendarSource *> &sources);
    [[nodiscard]] QList<CalendarSource *> sources() const;

    [[nodiscard]] QString sourceId() const override { return QStringLiteral("combined"); }
    [[nodiscard]] QList<CalendarInfo> calendars() const override;
    [[nodiscard]] QList<Event> eventsBetween(const QDateTime &from, const QDateTime &to,
                                             const QTimeZone &tz) const override;
    [[nodiscard]] QFuture<SourceSnapshot> load(const QDateTime &from, const QDateTime &to,
                                               const QTimeZone &tz) const override;
    void refresh() override;

    void createEvent(const EventDraft &draft, Created done) override;
    void importEvents(const QString &calendarId, const QByteArray &data, Imported done) override;
    void respond(const Event &event, const QString &status, bool wholeSeries,
                 Created done) override;
    void deleteEvent(const Event &event, bool wholeSeries, Created done) override;
    void moveEvent(const Event &event, const QDateTime &start, const QDateTime &end,
                   bool wholeSeries, Created done) override;
    void updateEvent(const Event &event, const EventEdit &edit, EditScope scope,
                     Created done) override;

    [[nodiscard]] bool syncing() const override;
    /// The oldest of the sources' last syncs, since that is how current all
    /// of them are.
    [[nodiscard]] QDateTime lastSynced() const override;
    [[nodiscard]] QString lastError() const override;
    [[nodiscard]] QVariantList syncReport() const override;
    [[nodiscard]] QStringList waitingChanges() const override;
    bool undoChange(const QString &id) override;

private:
    /// The source holding `calendarId`, or null with `done` told why.
    [[nodiscard]] CalendarSource *owner(const QString &calendarId, const Created &done) const;

    QList<QPointer<CalendarSource>> m_sources;
};

} // namespace callie
