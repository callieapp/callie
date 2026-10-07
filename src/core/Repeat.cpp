#include "callie/Repeat.h"

#include <QCoreApplication>
#include <QLocale>

using namespace Qt::StringLiterals;

namespace callie::Repeat {

namespace {

const char *const kDays[] = {"MO", "TU", "WE", "TH", "FR", "SA", "SU"};

QString dayCode(QDate date)
{
    return QString::fromLatin1(kDays[date.dayOfWeek() - 1]);
}

// Which of its kind in the month the day is: 1 for the first Tuesday, and
// -1 for the last, which every month has.
int weekOfMonth(QDate date)
{
    if (date.addDays(7).month() != date.month())
        return -1;
    return (date.day() - 1) / 7 + 1;
}

QString tr(const char *text)
{
    return QCoreApplication::translate("Repeat", text);
}

} // namespace

QStringList choices()
{
    return {u"none"_s,    u"daily"_s,          u"weekdays"_s, u"weekly"_s,
            u"monthly"_s, u"monthlyWeekday"_s, u"yearly"_s};
}

QStringList rule(const QString &choice, QDate start)
{
    QString line;
    if (choice == u"daily")
        line = u"FREQ=DAILY"_s;
    else if (choice == u"weekdays")
        line = u"FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR"_s;
    else if (choice == u"weekly")
        line = u"FREQ=WEEKLY;BYDAY="_s + dayCode(start);
    else if (choice == u"monthly")
        line = u"FREQ=MONTHLY;BYMONTHDAY="_s + QString::number(start.day());
    else if (choice == u"monthlyWeekday")
        line = u"FREQ=MONTHLY;BYDAY="_s + QString::number(weekOfMonth(start)) + dayCode(start);
    else if (choice == u"yearly")
        line = u"FREQ=YEARLY"_s;
    else
        return {};
    return {u"RRULE:"_s + line};
}

QString choiceOf(const QStringList &recurrence, QDate start)
{
    if (recurrence.isEmpty())
        return u"none"_s;
    for (const QString &choice : choices()) {
        if (choice != u"none" && rule(choice, start) == recurrence)
            return choice;
    }
    // Google writes plain weekly and yearly rules without the day.
    if (recurrence == QStringList{u"RRULE:FREQ=WEEKLY"_s})
        return u"weekly"_s;
    return u"custom"_s;
}

QString describe(const QString &choice, QDate start)
{
    const QLocale locale;
    const QString weekday = locale.dayName(start.dayOfWeek());
    if (choice == u"none")
        return tr("Does not repeat");
    if (choice == u"daily")
        return tr("Daily");
    if (choice == u"weekdays")
        return tr("Every weekday");
    if (choice == u"weekly")
        return tr("Weekly on %1").arg(weekday);
    if (choice == u"monthly")
        return tr("Monthly on day %1").arg(start.day());
    if (choice == u"monthlyWeekday") {
        static const char *const kWhich[] = {
            QT_TRANSLATE_NOOP("Repeat", "first"), QT_TRANSLATE_NOOP("Repeat", "second"),
            QT_TRANSLATE_NOOP("Repeat", "third"), QT_TRANSLATE_NOOP("Repeat", "fourth")};
        const int week = weekOfMonth(start);
        const QString which = week < 0 ? tr("last") : tr(kWhich[week - 1]);
        return tr("Monthly on the %1 %2").arg(which, weekday);
    }
    if (choice == u"yearly")
        return tr("Yearly on %1").arg(locale.toString(start, u"MMMM d"_s));
    return tr("Custom");
}

} // namespace callie::Repeat
