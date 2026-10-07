#pragma once

#include "CalendarSource.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QPointer>
#include <QStringList>

namespace callie {

/// Invitations waiting for an answer, soonest first, for the invites tray. A
/// repeating invitation is listed once, by its next occurrence, since one
/// answer usually covers the series.
class InvitesModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(callie::CalendarSource *source READ source WRITE setSource NOTIFY sourceChanged)
    /// Invitations that have ended drop out; QML sets this from its clock.
    Q_PROPERTY(QDateTime now READ now WRITE setNow NOTIFY nowChanged)
    /// Calendars the user hid, whose invitations are left out too.
    Q_PROPERTY(QStringList hiddenCalendars READ hiddenCalendars WRITE setHiddenCalendars NOTIFY
                   hiddenCalendarsChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        SummaryRole = Qt::UserRole + 1,
        StartRole,
        EndRole,
        AllDayRole,
        CalendarColorRole,
        RepeatsRole,
    };
    Q_ENUM(Role)

    /// How far ahead invitations are looked for.
    static constexpr int kDaysAhead = 90;

    explicit InvitesModel(QObject *parent = nullptr);

    [[nodiscard]] CalendarSource *source() const { return m_source; }
    void setSource(CalendarSource *source);
    [[nodiscard]] QDateTime now() const { return m_now; }
    void setNow(const QDateTime &now);
    [[nodiscard]] QStringList hiddenCalendars() const { return m_hidden; }
    void setHiddenCalendars(const QStringList &calendars);
    [[nodiscard]] int count() const { return int(m_invites.size()); }

    /// An invitation as EventModel::eventAt gives events, for EventActions.
    Q_INVOKABLE QVariantMap inviteAt(int row) const;

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void sourceChanged();
    void nowChanged();
    void hiddenCalendarsChanged();
    void countChanged();

private:
    void reload();
    void apply(const QList<Event> &events);

    QPointer<CalendarSource> m_source;
    QDateTime m_now;
    QDateTime m_loadedAt;
    QStringList m_hidden;
    QList<Event> m_loaded;
    QList<Event> m_invites;
    quint64 m_generation = 0;
};

} // namespace callie
