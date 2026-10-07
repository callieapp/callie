#pragma once

#include "CalendarSource.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QPointer>
#include <QStringList>

namespace callie {

/// Events matching a search, for the search box: every word typed must appear
/// in the title, place, notes or a guest's name or address. A repeating event
/// is listed once, by its next occurrence, or its last if all have passed.
/// Upcoming matches come first, soonest first, then past ones, latest first.
class SearchModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(callie::CalendarSource *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    /// Splits upcoming from past; QML sets it from its clock.
    Q_PROPERTY(QDateTime now READ now WRITE setNow NOTIFY nowChanged)
    Q_PROPERTY(QStringList hiddenCalendars READ hiddenCalendars WRITE setHiddenCalendars NOTIFY
                   hiddenCalendarsChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    /// Events are still being read; results will follow.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    enum Role {
        SummaryRole = Qt::UserRole + 1,
        StartRole,
        EndRole,
        AllDayRole,
        LocationRole,
        CalendarColorRole,
        UidRole,
        UpcomingRole,
    };
    Q_ENUM(Role)

    /// How far back and ahead events are searched.
    static constexpr int kDaysAround = 365;
    /// At most this many results are listed.
    static constexpr int kMaxResults = 100;

    explicit SearchModel(QObject *parent = nullptr);

    [[nodiscard]] CalendarSource *source() const { return m_source; }
    void setSource(CalendarSource *source);
    [[nodiscard]] QString query() const { return m_query; }
    void setQuery(const QString &query);
    [[nodiscard]] QDateTime now() const { return m_now; }
    void setNow(const QDateTime &now);
    [[nodiscard]] QStringList hiddenCalendars() const { return m_hidden; }
    void setHiddenCalendars(const QStringList &calendars);
    [[nodiscard]] int count() const { return int(m_results.size()); }
    [[nodiscard]] bool busy() const { return m_busy; }

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Whether `event` holds every one of `words`, ignoring case.
    [[nodiscard]] static bool matches(const Event &event, const QStringList &words);

Q_SIGNALS:
    void sourceChanged();
    void queryChanged();
    void nowChanged();
    void hiddenCalendarsChanged();
    void countChanged();
    void busyChanged();

private:
    /// Reads the events to search, unless what was read is still fresh.
    void ensureLoaded();
    void filter();
    void setBusy(bool busy);

    QPointer<CalendarSource> m_source;
    QString m_query;
    QDateTime m_now;
    QStringList m_hidden;
    QList<Event> m_pool;
    QDateTime m_loadedAt;
    bool m_stale = true;
    bool m_busy = false;
    quint64 m_generation = 0;
    QList<Event> m_results;
};

} // namespace callie
