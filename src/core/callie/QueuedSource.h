#pragma once

#include "CalendarSource.h"

#include <QPointer>
#include <QTimer>

#include <functional>

namespace callie {

/// A change made in Callie that has not reached the calendar's server yet.
struct PendingChange
{
    enum class Kind { Create, Respond, Delete, Move };

    QString id;
    Kind kind = Kind::Create;
    /// The event as it was; for a creation, the event as it will be.
    Event event;
    /// A creation's details.
    EventDraft draft;
    /// An answer: "accepted", "tentative" or "declined".
    QString status;
    bool wholeSeries = false;
    /// A move's new times.
    QDateTime start;
    QDateTime end;
    /// Not sent before this, so it can still be taken back.
    QDateTime notBefore;

    /// What the change does, such as "Move Standup".
    [[nodiscard]] QString describe() const;
    /// What was done, such as "Moved Standup".
    [[nodiscard]] QString describeDone() const;
};

/// Takes every change at once and sends it on in the background: what the
/// views read already shows it, it survives a restart, and while the network
/// is down it waits and goes out once it is back. A change the server refuses
/// is dropped, so the event shows as it really is, and the refusal is shown as
/// the source's last error until the next change goes through.
class QueuedSource : public CalendarSource
{
    Q_OBJECT

public:
    using Now = std::function<QDateTime()>;
    using Online = std::function<bool()>;

    /// Keeps the queue in the JSON file at `path`, or only in memory when it
    /// is empty.
    QueuedSource(CalendarSource &inner, QString path, QObject *parent = nullptr);

    /// `$XDG_DATA_HOME/callie/pending.json`
    [[nodiscard]] static QString defaultPath();

    /// Replace the clock and the network check, for tests.
    void setNow(Now now) { m_now = std::move(now); }
    void setOnline(Online online) { m_online = std::move(online); }

    [[nodiscard]] QString sourceId() const override { return m_inner->sourceId(); }
    [[nodiscard]] QList<CalendarInfo> calendars() const override { return m_inner->calendars(); }
    [[nodiscard]] QList<Event> eventsBetween(const QDateTime &from, const QDateTime &to,
                                             const QTimeZone &tz) const override;
    [[nodiscard]] QFuture<SourceSnapshot> load(const QDateTime &from, const QDateTime &to,
                                               const QTimeZone &tz) const override;
    void refresh() override;
    void createEvent(const EventDraft &draft, Created done) override;
    void respond(const Event &event, const QString &status, bool wholeSeries,
                 Created done) override;
    void deleteEvent(const Event &event, bool wholeSeries, Created done) override;
    void moveEvent(const Event &event, const QDateTime &start, const QDateTime &end,
                   bool wholeSeries, Created done) override;
    [[nodiscard]] bool syncing() const override { return m_inner->syncing(); }
    [[nodiscard]] QDateTime lastSynced() const override { return m_inner->lastSynced(); }
    [[nodiscard]] QString lastError() const override;
    [[nodiscard]] QVariantList syncReport() const override { return m_inner->syncReport(); }
    [[nodiscard]] QStringList waitingChanges() const override;

    [[nodiscard]] QList<PendingChange> changes() const { return m_changes; }
    /// Takes back a change that has not been sent; false once it has been.
    bool undoChange(const QString &id) override;
    /// Sends what is ready, in order, one at a time.
    void flush();

    /// Puts the changes on what the source read.
    static void apply(QList<Event> &events, const QList<PendingChange> &changes,
                      const QList<CalendarInfo> &calendars, const QDateTime &from,
                      const QDateTime &to, const QTimeZone &tz);

    /// How long every change waits before it is sent, so it can be taken back.
    static constexpr int kHoldSecs = 8;

private:
    void add(PendingChange change, const Created &done);
    /// Notes how a creation not yet sent was before a later change folded into
    /// it, and offers that change to be undone like any other.
    void folded(qsizetype index, const PendingChange &before, const QString &what);
    void send(const PendingChange &change);
    void finished(const QString &id, const Outcome &outcome);
    void save() const;
    void restore();

    QPointer<CalendarSource> m_inner;
    QString m_path;
    QList<PendingChange> m_changes;
    /// The change on its way to the server, which can no longer be cancelled.
    QString m_sending;
    /// Creations as they were before a change folded into them, by the id
    /// that undoes that change, with where they stood in the queue.
    QHash<QString, std::pair<qsizetype, PendingChange>> m_folded;
    QString m_error;
    Now m_now;
    Online m_online;
    QTimer m_retry;
};

} // namespace callie
