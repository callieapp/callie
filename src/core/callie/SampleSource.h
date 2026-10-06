#pragma once

#include "CalendarSource.h"

#include <QHash>
#include <QSet>

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
    /// Made-up data is always up to date, so a refresh only marks the time.
    void refresh() override;
    [[nodiscard]] QDateTime lastSynced() const override { return m_synced; }
    [[nodiscard]] QVariantList syncReport() const override;
    /// Keeps new events for as long as the app runs.
    void createEvent(const EventDraft &draft, Created done) override;
    /// Answers and deletions last as long as the app runs.
    void respond(const Event &event, const QString &status, bool wholeSeries,
                 Created done) override;
    void deleteEvent(const Event &event, bool wholeSeries, Created done) override;

private:
    QList<Event> m_created;
    QHash<QString, QString> m_answers;
    QSet<QString> m_deleted;
    QDateTime m_synced;
    QList<CalendarInfo> m_calendars;
};

} // namespace callie
