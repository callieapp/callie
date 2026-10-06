#pragma once

#include "CalendarSource.h"

namespace callie {

/// Placeholder source with plausible data, so the UI can be designed before
/// the Google and CalDAV backends exist. Delete once those land.
class SampleSource : public CalendarSource
{
    Q_OBJECT

public:
    explicit SampleSource(QObject *parent = nullptr);

    [[nodiscard]] QString sourceId() const override { return QStringLiteral("sample"); }
    [[nodiscard]] QList<CalendarInfo> calendars() const override { return m_calendars; }
    [[nodiscard]] QList<Event> eventsBetween(const QDateTime &from, const QDateTime &to,
                                             const QTimeZone &tz) const override;
    void refresh() override { Q_EMIT changed(); }
    /// Keeps new events for as long as the app runs.
    void createEvent(const EventDraft &draft, Created done) override;

private:
    QList<Event> m_created;
    QList<CalendarInfo> m_calendars;
};

} // namespace callie
