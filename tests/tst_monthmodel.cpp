#include "callie/MonthModel.h"

#include <QTest>

using namespace callie;

namespace {

QVariant cell(const MonthModel &model, int row, MonthModel::Role role)
{
    return model.data(model.index(row), role);
}

} // namespace

class TestMonthModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void gridStartsOnMonday();
    void rowsCoverTheWholeMonth();
    void weekAndTodayAreMarked();
};

void TestMonthModel::gridStartsOnMonday()
{
    // March 2026 starts on a Sunday, so the grid opens on Monday 23 February.
    MonthModel model;
    model.setMonth(QDate(2026, 3, 18));

    QCOMPARE(cell(model, 0, MonthModel::DayRole).toInt(), 23);
    QVERIFY(!cell(model, 0, MonthModel::InMonthRole).toBool());
    QCOMPARE(cell(model, 6, MonthModel::DayRole).toInt(), 1);
    QVERIFY(cell(model, 6, MonthModel::InMonthRole).toBool());
}

void TestMonthModel::rowsCoverTheWholeMonth()
{
    MonthModel model;
    model.setMonth(QDate(2026, 3, 1));
    QCOMPARE(model.rowCount(), 42); // 1 March is a Sunday, 31 March a Tuesday

    model.setMonth(QDate(2027, 2, 10));
    QCOMPARE(model.rowCount(), 28); // February 2027 starts on a Monday and has 28 days

    model.setMonth(QDate(2026, 10, 6));
    QCOMPARE(model.rowCount(), 35);
}

void TestMonthModel::weekAndTodayAreMarked()
{
    MonthModel model;
    model.setMonth(QDate(2026, 3, 18));
    model.setWeekStart(QDate(2026, 3, 16));
    model.setToday(QDate(2026, 3, 18));

    int inWeek = 0, today = -1;
    for (int row = 0; row < model.rowCount(); ++row) {
        inWeek += cell(model, row, MonthModel::InWeekRole).toBool() ? 1 : 0;
        if (cell(model, row, MonthModel::IsTodayRole).toBool())
            today = cell(model, row, MonthModel::DayRole).toInt();
    }
    QCOMPARE(inWeek, 7);
    QCOMPARE(today, 18);
}

QTEST_GUILESS_MAIN(TestMonthModel)
#include "tst_monthmodel.moc"
