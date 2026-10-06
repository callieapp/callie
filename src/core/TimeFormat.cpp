#include "callie/TimeFormat.h"

#include <QLocale>

using namespace Qt::StringLiterals;

namespace callie {

QString formatClock(QTime time, bool use24Hour)
{
    return use24Hour ? time.toString(u"H:mm"_s) : QLocale().toString(time, u"h:mm AP"_s);
}

QString formatHourLabel(int hour, bool use24Hour)
{
    const QTime time(hour % 24, 0);
    return use24Hour ? time.toString(u"H:mm"_s) : QLocale().toString(time, u"h AP"_s);
}

} // namespace callie
