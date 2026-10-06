#include "callie/ViewRange.h"

#include <QCoreApplication>
#include <QLocale>

using namespace Qt::StringLiterals;

namespace callie::ViewRange {

namespace {

QDate mondayOf(QDate day)
{
    return day.addDays(-(day.dayOfWeek() - Qt::Monday));
}

QDate firstOfMonth(QDate day)
{
    return QDate(day.year(), day.month(), 1);
}

} // namespace

View fromName(const QString &name)
{
    if (name == u"day")
        return View::Day;
    if (name == u"month")
        return View::Month;
    if (name == u"agenda")
        return View::Agenda;
    return View::Week;
}

QDate start(View view, QDate focus)
{
    switch (view) {
    case View::Week: return mondayOf(focus);
    case View::Month: return mondayOf(firstOfMonth(focus));
    case View::Day:
    case View::Agenda: break;
    }
    return focus;
}

int days(View view, QDate focus)
{
    switch (view) {
    case View::Day: return 1;
    case View::Week: return 7;
    case View::Agenda: return kAgendaDays;
    case View::Month: break;
    }
    const QDate first = firstOfMonth(focus);
    const qint64 lead = first.dayOfWeek() - Qt::Monday;
    return int((lead + first.daysInMonth() + 6) / 7 * 7);
}

QDate step(View view, QDate focus, int count)
{
    switch (view) {
    case View::Day: return focus.addDays(count);
    case View::Week: return focus.addDays(7 * qint64(count));
    case View::Agenda: return focus.addDays(qint64(kAgendaDays) * count);
    case View::Month: break;
    }
    return firstOfMonth(focus).addMonths(count);
}

QString heading(QDate day, QDate today)
{
    const qint64 offset = today.daysTo(day);
    if (offset == 0)
        return QCoreApplication::translate("ViewRange", "Today");
    if (offset == 1)
        return QCoreApplication::translate("ViewRange", "Tomorrow");
    if (offset == -1)
        return QCoreApplication::translate("ViewRange", "Yesterday");
    return QLocale().toString(day, u"dddd, MMMM d"_s);
}

} // namespace callie::ViewRange
