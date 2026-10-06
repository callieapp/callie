#include "Views.h"

#include "callie/ViewRange.h"

namespace callie {

QDateTime Views::start(const QString &view, const QDateTime &focus) const
{
    return ViewRange::start(ViewRange::fromName(view), focus.date()).startOfDay();
}

int Views::days(const QString &view, const QDateTime &focus) const
{
    return ViewRange::days(ViewRange::fromName(view), focus.date());
}

QDateTime Views::step(const QString &view, const QDateTime &focus, int count) const
{
    return ViewRange::step(ViewRange::fromName(view), focus.date(), count).startOfDay();
}

QString Views::heading(const QDateTime &day, const QDateTime &today) const
{
    return ViewRange::heading(day.date(), today.date());
}

} // namespace callie
