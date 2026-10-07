#pragma once

#include "CalendarSource.h"

#include <QPointer>

namespace callie {

class Settings;

/// A source as the user sees it: calendars carry the names and colors the
/// user gave them in Settings, and so do their events. Everything else goes
/// straight to the source underneath, so every view, the composer and the
/// reminders agree.
class LookedSource : public CalendarSource
{
    Q_OBJECT

public:
    LookedSource(CalendarSource &inner, Settings &settings, QObject *parent = nullptr);

    [[nodiscard]] QString sourceId() const override { return m_inner->sourceId(); }
    [[nodiscard]] QList<CalendarInfo> calendars() const override;
    [[nodiscard]] QList<Event> eventsBetween(const QDateTime &from, const QDateTime &to,
                                             const QTimeZone &tz) const override;
    [[nodiscard]] QFuture<SourceSnapshot> load(const QDateTime &from, const QDateTime &to,
                                               const QTimeZone &tz) const override;
    void refresh() override { m_inner->refresh(); }
    void createEvent(const EventDraft &draft, Created done) override;
    void respond(const Event &event, const QString &status, bool wholeSeries,
                 Created done) override;
    void deleteEvent(const Event &event, bool wholeSeries, Created done) override;
    void moveEvent(const Event &event, const QDateTime &start, const QDateTime &end,
                   bool wholeSeries, Created done) override;
    [[nodiscard]] bool syncing() const override { return m_inner->syncing(); }
    [[nodiscard]] QDateTime lastSynced() const override { return m_inner->lastSynced(); }
    [[nodiscard]] QString lastError() const override { return m_inner->lastError(); }
    [[nodiscard]] QVariantList syncReport() const override { return m_inner->syncReport(); }

    /// Puts the looks in `looks` (as Settings::calendarLooks) on a snapshot.
    static void applyLooks(SourceSnapshot &snapshot, const QVariantMap &looks);

private:
    QPointer<CalendarSource> m_inner;
    QPointer<Settings> m_settings;
};

} // namespace callie
