#include "callie/MonthModel.h"

#include "callie/ViewRange.h"

#include <algorithm>

namespace callie {

MonthModel::MonthModel(QObject *parent) : QAbstractListModel(parent) {}

void MonthModel::setMonth(QDate day)
{
    if (!day.isValid())
        return;
    const QDate first(day.year(), day.month(), 1);
    if (first == m_first)
        return;
    beginResetModel();
    m_first = first;
    endResetModel();
    Q_EMIT monthChanged();
}

void MonthModel::setFirstDay(int day)
{
    day = std::clamp(day, int(Qt::Monday), int(Qt::Sunday));
    if (day == m_firstDay)
        return;
    beginResetModel();
    m_firstDay = day;
    endResetModel();
    Q_EMIT firstDayChanged();
}

void MonthModel::setWeekStart(QDate first)
{
    if (first == m_weekStart)
        return;
    m_weekStart = first;
    Q_EMIT weekStartChanged();
    flagsChanged();
}

void MonthModel::setToday(QDate today)
{
    if (today == m_today)
        return;
    m_today = today;
    Q_EMIT todayChanged();
    flagsChanged();
}

void MonthModel::flagsChanged()
{
    if (rowCount() > 0)
        Q_EMIT dataChanged(index(0), index(rowCount() - 1), {InWeekRole, IsTodayRole});
}

QDate MonthModel::gridStart() const
{
    return ViewRange::weekStart(m_first, Qt::DayOfWeek(m_firstDay));
}

QList<int> MonthModel::weekNumbers(QDate month, int firstDay) const
{
    Q_UNUSED(month)
    Q_UNUSED(firstDay)
    QList<int> numbers;
    for (int row = 0; row < rowCount() / 7; ++row)
        numbers.append(gridStart().addDays(row * 7 + 3).weekNumber());
    return numbers;
}

int MonthModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_first.isValid())
        return 0;
    const QDate last = m_first.addDays(m_first.daysInMonth() - 1);
    const qint64 days = gridStart().daysTo(last) + 1;
    return int((days + 6) / 7 * 7);
}

QVariant MonthModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount())
        return {};
    const QDate date = gridStart().addDays(index.row());
    switch (role) {
    case DateRole: return date.startOfDay();
    case DayRole: return date.day();
    case InMonthRole: return date.month() == m_first.month();
    case InWeekRole:
        return m_weekStart.isValid() && date >= m_weekStart && date < m_weekStart.addDays(7);
    case IsTodayRole: return date == m_today;
    default: return {};
    }
}

QHash<int, QByteArray> MonthModel::roleNames() const
{
    return {{DateRole, "date"},
            {DayRole, "day"},
            {InMonthRole, "inMonth"},
            {InWeekRole, "inWeek"},
            {IsTodayRole, "isToday"}};
}

} // namespace callie
