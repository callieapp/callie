#pragma once

#include <QAbstractListModel>
#include <QDate>

namespace callie {

/// The days of one month as whole weeks, for the mini month: five or six rows
/// of seven, with the neighbouring months' days marked.
class MonthModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QDate month READ month WRITE setMonth NOTIFY monthChanged)
    Q_PROPERTY(QDate weekStart READ weekStart WRITE setWeekStart NOTIFY weekStartChanged)
    Q_PROPERTY(QDate today READ today WRITE setToday NOTIFY todayChanged)
    /// The day weeks start on, 1 (Monday) to 7 (Sunday).
    Q_PROPERTY(int firstDay READ firstDay WRITE setFirstDay NOTIFY firstDayChanged)

public:
    enum Role { DateRole = Qt::UserRole + 1, DayRole, InMonthRole, InWeekRole, IsTodayRole };
    Q_ENUM(Role)

    explicit MonthModel(QObject *parent = nullptr);

    /// Any day of the month to show.
    [[nodiscard]] QDate month() const { return m_first; }
    void setMonth(QDate day);
    /// The first day of the week the main view shows, which is highlighted.
    [[nodiscard]] QDate weekStart() const { return m_weekStart; }
    void setWeekStart(QDate first);
    [[nodiscard]] int firstDay() const { return m_firstDay; }
    void setFirstDay(int day);
    [[nodiscard]] QDate today() const { return m_today; }
    void setToday(QDate today);

    /// The ISO week number of each row, read from its middle day. QML passes
    /// the month and first day so a binding updates when they change.
    Q_INVOKABLE QList<int> weekNumbers(QDate month, int firstDay) const;

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void monthChanged();
    void weekStartChanged();
    void todayChanged();
    void firstDayChanged();

private:
    [[nodiscard]] QDate gridStart() const;
    void flagsChanged();

    QDate m_first;
    QDate m_weekStart;
    QDate m_today;
    int m_firstDay = Qt::Monday;
};

} // namespace callie
