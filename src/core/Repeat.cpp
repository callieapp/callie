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

QString tr(const char *text, int n)
{
    return QCoreApplication::translate("Repeat", text, nullptr, n);
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

namespace {

int dayFromCode(const QString &code)
{
    for (int day = 1; day <= 7; ++day) {
        if (code == QLatin1StringView(kDays[day - 1]))
            return day;
    }
    return 0;
}

} // namespace

std::optional<Custom> custom(const QStringList &recurrence, QDate start, const QTimeZone &zone)
{
    QString rrule;
    for (const QString &line : recurrence) {
        if (!line.startsWith(u"RRULE:"_s, Qt::CaseInsensitive))
            continue;
        // Two rules cannot be said in one form.
        if (!rrule.isEmpty())
            return std::nullopt;
        rrule = line.mid(6);
    }
    if (rrule.isEmpty())
        return std::nullopt;

    Custom c;
    QString byDay;
    QString byMonthDay;
    QString byMonth;
    for (const QString &part : rrule.split(u';', Qt::SkipEmptyParts)) {
        const QString name = part.section(u'=', 0, 0).toUpper();
        const QString value = part.section(u'=', 1);
        if (name == u"FREQ") {
            static const QStringList kFrequencies = {u"DAILY"_s, u"WEEKLY"_s, u"MONTHLY"_s,
                                                     u"YEARLY"_s};
            if (!kFrequencies.contains(value.toUpper()))
                return std::nullopt;
            c.frequency = value.toLower();
        } else if (name == u"INTERVAL") {
            c.interval = value.toInt();
            if (c.interval < 1)
                return std::nullopt;
        } else if (name == u"COUNT") {
            c.count = value.toInt();
            if (c.count < 1)
                return std::nullopt;
        } else if (name == u"UNTIL") {
            if (value.size() == 8) {
                c.until = QDate::fromString(value, u"yyyyMMdd"_s);
            } else {
                QDateTime until = QDateTime::fromString(value.chopped(value.endsWith(u'Z')),
                                                        u"yyyyMMdd'T'HHmmss"_s);
                until.setTimeZone(value.endsWith(u'Z') ? QTimeZone::UTC : zone);
                c.until = until.toTimeZone(zone).date();
            }
            if (!c.until.isValid())
                return std::nullopt;
        } else if (name == u"BYDAY") {
            byDay = value.toUpper();
        } else if (name == u"BYMONTHDAY") {
            byMonthDay = value;
        } else if (name == u"BYMONTH") {
            byMonth = value;
        } else if (name == u"WKST") {
            if (dayFromCode(value.toUpper()) == 0)
                return std::nullopt;
            if (value.toUpper() != u"MO")
                c.weekStart = value.toUpper();
        } else {
            return std::nullopt;
        }
    }

    // A month or a day of the month only restates the start where the rule
    // already falls in it: a yearly rule's month, a monthly or yearly one's day.
    if (!byMonth.isEmpty() && (c.frequency != u"yearly" || byMonth.toInt() != start.month()))
        return std::nullopt;
    if (!byMonthDay.isEmpty() && (c.frequency == u"daily" || c.frequency == u"weekly"))
        return std::nullopt;

    if (c.frequency == u"weekly") {
        for (const QString &code : byDay.split(u',', Qt::SkipEmptyParts)) {
            const int day = dayFromCode(code);
            if (day == 0)
                return std::nullopt;
            c.weekdays << day;
        }
        if (c.weekdays.isEmpty())
            c.weekdays << start.dayOfWeek();
        std::sort(c.weekdays.begin(), c.weekdays.end());
    } else if (c.frequency == u"monthly") {
        // Only the start's own day, or its own weekday of the month.
        if (!byDay.isEmpty()) {
            if (byDay != QString::number(weekOfMonth(start)) + dayCode(start) ||
                !byMonthDay.isEmpty())
                return std::nullopt;
            c.onWeekday = true;
        } else if (!byMonthDay.isEmpty() && byMonthDay.toInt() != start.day()) {
            return std::nullopt;
        }
    } else if (!byDay.isEmpty() || (!byMonthDay.isEmpty() && byMonthDay.toInt() != start.day())) {
        return std::nullopt;
    }
    if (c.count > 0 && c.until.isValid())
        return std::nullopt;
    return c;
}

QStringList rule(const Custom &c, QDate start, bool allDay, const QTimeZone &zone)
{
    QStringList parts{u"FREQ="_s + c.frequency.toUpper()};
    if (c.interval > 1)
        parts << u"INTERVAL="_s + QString::number(c.interval);
    if (c.frequency == u"weekly") {
        QList<int> days = c.weekdays;
        if (days.isEmpty())
            days << start.dayOfWeek();
        std::sort(days.begin(), days.end());
        QStringList codes;
        for (int day : days)
            codes << QString::fromLatin1(kDays[day - 1]);
        parts << u"BYDAY="_s + codes.join(u',');
        if (!c.weekStart.isEmpty())
            parts << u"WKST="_s + c.weekStart;
    } else if (c.frequency == u"monthly") {
        parts << (c.onWeekday ? u"BYDAY="_s + QString::number(weekOfMonth(start)) + dayCode(start)
                              : u"BYMONTHDAY="_s + QString::number(start.day()));
    }
    if (c.count > 0) {
        parts << u"COUNT="_s + QString::number(c.count);
    } else if (c.until.isValid()) {
        // The whole last day counts, wherever the event's own clock is.
        parts << u"UNTIL="_s + (allDay ? c.until.toString(u"yyyyMMdd"_s)
                                       : QDateTime(c.until, QTime(23, 59, 59), zone)
                                             .toUTC()
                                             .toString(u"yyyyMMdd'T'HHmmss'Z'"_s));
    }
    return {u"RRULE:"_s + parts.join(u';')};
}

QString describe(const Custom &c, QDate start)
{
    const QLocale locale;
    QString every;
    if (c.frequency == u"daily")
        every = c.interval == 1 ? tr("Daily") : tr("Every %1 days").arg(c.interval);
    else if (c.frequency == u"weekly")
        every = c.interval == 1 ? tr("Weekly") : tr("Every %1 weeks").arg(c.interval);
    else if (c.frequency == u"monthly")
        every = c.interval == 1 ? tr("Monthly") : tr("Every %1 months").arg(c.interval);
    else
        every = c.interval == 1 ? tr("Yearly") : tr("Every %1 years").arg(c.interval);

    QString on;
    if (c.frequency == u"weekly") {
        QStringList names;
        for (int day : c.weekdays.isEmpty() ? QList<int>{start.dayOfWeek()} : c.weekdays)
            names << locale.dayName(day);
        on = names.size() == 1
                 ? names.first()
                 : tr("%1 and %2").arg(names.mid(0, names.size() - 1).join(u", "_s), names.last());
        on = tr(" on %1").arg(on);
    } else if (c.frequency == u"monthly") {
        static const char *const kWhich[] = {
            QT_TRANSLATE_NOOP("Repeat", "first"), QT_TRANSLATE_NOOP("Repeat", "second"),
            QT_TRANSLATE_NOOP("Repeat", "third"), QT_TRANSLATE_NOOP("Repeat", "fourth")};
        const int week = weekOfMonth(start);
        on = c.onWeekday ? tr(" on the %1 %2")
                               .arg(week < 0 ? tr("last") : tr(kWhich[week - 1]),
                                    locale.dayName(start.dayOfWeek()))
                         : tr(" on day %1").arg(start.day());
    }

    QString ends;
    if (c.count > 0)
        ends = tr(", %n times", c.count);
    else if (c.until.isValid())
        ends = tr(", until %1").arg(locale.toString(c.until, u"MMM d, yyyy"_s));
    return every + on + ends;
}

} // namespace callie::Repeat
