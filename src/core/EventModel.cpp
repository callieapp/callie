#include "callie/EventModel.h"
#include "callie/TimeFormat.h"

#include <QLocale>
#include <QTextDocumentFragment>
#include <QUrl>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

/// Google sends descriptions as HTML. Plain text keeps untrusted markup, such as
/// remote images, out of the view.
QString plainDescription(const QString &description)
{
    if (!description.contains(u'<')) {
        if (!description.contains(u'&'))
            return description.trimmed();
        // Escaped text with no tags: decode entities a line at a time, since
        // HTML would fold the line breaks.
        QStringList lines = description.split(u'\n');
        for (QString &line : lines)
            line = QTextDocumentFragment::fromHtml(line).toPlainText();
        return lines.join(u'\n').trimmed();
    }
    QString text = QTextDocumentFragment::fromHtml(description).toPlainText();
    // Images become object replacement characters; there is nothing to show for them.
    text.remove(QChar::ObjectReplacementCharacter);
    return text.trimmed();
}

} // namespace

EventModel::EventModel(QObject *parent) : QAbstractListModel(parent) {}

void EventModel::setSource(CalendarSource *source)
{
    if (m_source == source)
        return;
    if (m_source)
        disconnect(m_source, nullptr, this, nullptr);
    m_source = source;
    if (m_source)
        connect(m_source, &CalendarSource::changed, this, &EventModel::reload);
    Q_EMIT sourceChanged();
    reloadRange();
}

void EventModel::setRangeStart(QDate date)
{
    // QML passes an unset date before its first assignment.
    if (m_rangeStart == date || !date.isValid())
        return;
    m_rangeStart = date;
    Q_EMIT rangeChanged();
    reloadRange();
}

void EventModel::setDayCount(int days)
{
    if (m_dayCount == days || days <= 0)
        return;
    m_dayCount = days;
    Q_EMIT rangeChanged();
    reloadRange();
}

void EventModel::setTimeZone(const QTimeZone &zone)
{
    if (m_tz == zone || !zone.isValid())
        return;
    m_tz = zone;
    Q_EMIT timeZoneChanged();
    reloadRange();
}

QString EventModel::timeZoneId() const
{
    return m_tz == QTimeZone::systemTimeZone() ? QString() : QString::fromUtf8(m_tz.id());
}

void EventModel::setTimeZoneId(const QString &id)
{
    setTimeZone(id.isEmpty() ? QTimeZone::systemTimeZone() : QTimeZone(id.toUtf8()));
}

void EventModel::setHiddenCalendars(const QStringList &ids)
{
    if (m_hiddenCalendars == ids)
        return;
    m_hiddenCalendars = ids;
    Q_EMIT filterChanged();
    reload();
}

void EventModel::setShowDeclined(bool show)
{
    if (m_showDeclined == show)
        return;
    m_showDeclined = show;
    Q_EMIT filterChanged();
    reload();
}

void EventModel::reload()
{
    load(false);
}

void EventModel::reloadRange()
{
    load(true);
}

void EventModel::load(bool rangeChanged)
{
    const quint64 generation = ++m_generation;
    if (!m_source) {
        apply({});
        return;
    }
    const QDateTime from(m_rangeStart, QTime(0, 0), m_tz);
    const QDateTime to(m_rangeStart.addDays(m_dayCount), QTime(0, 0), m_tz);
    QFuture<SourceSnapshot> future = m_source->load(from, to, m_tz);
    if (future.isFinished()) {
        apply(future.result());
        return;
    }
    // Rows place themselves relative to the range, so after a range change
    // they would land on the wrong days while the new range loads.
    if (rangeChanged && !m_events.isEmpty()) {
        beginResetModel();
        m_events.clear();
        m_plainDescriptions.clear();
        ++m_revision;
        endResetModel();
        Q_EMIT revisionChanged();
    }
    future.then(this, [this, generation](const SourceSnapshot &snapshot) {
        if (generation == m_generation)
            apply(snapshot);
    });
}

void EventModel::apply(SourceSnapshot snapshot)
{
    QList<Event> &events = snapshot.events;
    QVariantList calendars;
    events.removeIf([this](const Event &e) {
        return m_hiddenCalendars.contains(e.calendarId) || (e.declined && !m_showDeclined);
    });
    // Working locations label their days instead of taking a row.
    QStringList workPlaces(m_dayCount);
    for (const Event &e : std::as_const(events)) {
        if (!e.workPlace || e.summary.isEmpty())
            continue;
        const auto [first, span] = visibleDays(e);
        for (int day = first; day < first + span; ++day) {
            if (workPlaces[day].isEmpty())
                workPlaces[day] = e.summary;
            else if (!workPlaces[day].split(u", "_s).contains(e.summary))
                workPlaces[day] += u", "_s + e.summary;
        }
    }
    events.removeIf([](const Event &e) { return e.workPlace; });
    assignLanes(events);
    // Calendars first: rows name their calendar, and views read rows as soon as
    // the reset ends.
    beginResetModel();
    m_calendarNames.clear();
    for (const CalendarInfo &calendar : std::as_const(snapshot.calendars)) {
        m_calendarNames.insert(calendar.id, calendar.displayName);
        calendars.append(QVariantMap{{QStringLiteral("id"), calendar.id},
                                     {QStringLiteral("name"), calendar.displayName},
                                     {QStringLiteral("color"), calendar.color},
                                     {QStringLiteral("account"), calendar.account}});
    }
    m_events = std::move(events);
    m_plainDescriptions.clear();
    const int rows = assignAllDayRows(m_events);
    ++m_revision;
    endResetModel();
    Q_EMIT revisionChanged();
    if (rows != m_allDayRows) {
        m_allDayRows = rows;
        Q_EMIT allDayRowsChanged();
    }
    if (calendars != m_calendars) {
        m_calendars = calendars;
        Q_EMIT calendarsChanged();
    }
    if (workPlaces != m_workPlaces) {
        m_workPlaces = workPlaces;
        Q_EMIT workPlacesChanged();
    }
}

void EventModel::setMinimumMinutes(int minutes)
{
    if (m_minimumMinutes == minutes)
        return;
    m_minimumMinutes = minutes;
    Q_EMIT minimumMinutesChanged();
    reload();
}

void EventModel::assignLanes(QList<Event> &events) const
{
    // Overlapping events share the width in columns. A later event may stack
    // on top of a column instead, stepping in, once that column's events
    // started long enough before it for their titles to show above it.
    constexpr qint64 kTitleShowsSecs = 30 * 60;
    QMap<qint64, QList<Event *>> byDay;
    for (Event &e : events) {
        if (e.allDay)
            continue;
        byDay[e.start.date().toJulianDay()].append(&e);
    }

    // Where each event's block ends on screen, which for a short event is
    // later than the event itself.
    const auto drawnEnd = [this](const Event *e) {
        return std::max(e->end, e->start.addSecs(qint64(m_minimumMinutes) * 60));
    };

    for (QList<Event *> &day : byDay) {
        std::sort(day.begin(), day.end(), [](const Event *a, const Event *b) {
            return a->start == b->start ? a->end > b->end : a->start < b->start;
        });

        // The events overlapping one another, directly or through others,
        // share a column count.
        QList<QList<Event *>> columns;
        QList<Event *> cluster;
        QDateTime clusterEnd;
        const auto finish = [&] {
            for (Event *e : std::as_const(cluster))
                e->laneCount = int(columns.size());
            columns.clear();
            cluster.clear();
        };

        for (Event *e : day) {
            if (!cluster.isEmpty() && e->start >= clusterEnd)
                finish();
            if (cluster.isEmpty())
                clusterEnd = drawnEnd(e);
            clusterEnd = std::max(clusterEnd, drawnEnd(e));

            // A free column first, then one to stack on, then a new one.
            qsizetype free = -1;
            qsizetype stack = -1;
            int stackDepth = 0;
            for (qsizetype c = 0; c < columns.size(); ++c) {
                bool overlaps = false;
                bool titlesShow = true;
                int depth = 0;
                for (const Event *other : std::as_const(columns[c])) {
                    if (drawnEnd(other) <= e->start)
                        continue;
                    overlaps = true;
                    titlesShow = titlesShow && other->start.secsTo(e->start) >= kTitleShowsSecs;
                    depth = std::max(depth, other->depth + 1);
                }
                if (!overlaps) {
                    free = c;
                    break;
                }
                if (titlesShow && stack < 0) {
                    stack = c;
                    stackDepth = depth;
                }
            }
            if (free >= 0) {
                e->lane = int(free);
                e->depth = 0;
            } else if (stack >= 0) {
                e->lane = int(stack);
                e->depth = stackDepth;
            } else {
                e->lane = int(columns.size());
                e->depth = 0;
                columns.append(QList<Event *>());
            }
            columns[e->lane].append(e);
            cluster.append(e);
        }
        finish();
    }
}

std::pair<int, int> EventModel::visibleDays(const Event &event) const
{
    const auto first = static_cast<int>(m_rangeStart.daysTo(event.start.date()));
    // All-day ends are exclusive dates, checked without the time because some
    // DST days have no midnight. A timed end at midnight belongs to the day before.
    QDate endDay = event.end.date();
    if (event.end > event.start && (event.allDay || event.end.time() == QTime(0, 0)))
        endDay = endDay.addDays(-1);
    const auto last = static_cast<int>(m_rangeStart.daysTo(endDay));
    const int clampedFirst = std::clamp(first, 0, m_dayCount - 1);
    const int clampedLast = std::clamp(last, clampedFirst, m_dayCount - 1);
    return {clampedFirst, clampedLast - clampedFirst + 1};
}

int EventModel::assignAllDayRows(QList<Event> &events) const
{
    QList<Event *> allDay;
    for (Event &e : events) {
        if (e.allDay)
            allDay.append(&e);
    }
    // Longer events first, so multi-day bars sit above single days.
    std::sort(allDay.begin(), allDay.end(), [this](const Event *a, const Event *b) {
        const auto [aFirst, aSpan] = visibleDays(*a);
        const auto [bFirst, bSpan] = visibleDays(*b);
        return aFirst == bFirst ? aSpan > bSpan : aFirst < bFirst;
    });

    QList<int> rowEnds;
    for (Event *e : allDay) {
        const auto [first, span] = visibleDays(*e);
        int row = 0;
        while (row < rowEnds.size() && rowEnds[row] > first)
            ++row;
        if (row == rowEnds.size())
            rowEnds.append(first + span);
        else
            rowEnds[row] = first + span;
        e->lane = row;
        e->laneCount = 1;
    }
    return int(rowEnds.size());
}

bool EventModel::isDayOff(QDate date) const
{
    return !QLocale().weekdays().contains(Qt::DayOfWeek(date.dayOfWeek()));
}

QString EventModel::callService(const QUrl &url) const
{
    const QString scheme = url.scheme();
    if (!url.isValid() || (scheme != u"https"_s && scheme != u"http"_s))
        return {};
    const QString host = url.host().toLower();
    if (host == u"zoom.us"_s || host.endsWith(u".zoom.us"_s))
        return u"zoom"_s;
    if (host == u"meet.google.com"_s)
        return u"meet"_s;
    if (host == u"teams.microsoft.com"_s || host == u"teams.live.com"_s)
        return u"teams"_s;
    return u"web"_s;
}

QVariantList EventModel::eventsOn(int dayIndex, int revision) const
{
    Q_UNUSED(revision)
    const QDateTime dayStart(m_rangeStart.addDays(dayIndex), QTime(0, 0), m_tz);
    const QDateTime dayEnd(m_rangeStart.addDays(dayIndex + 1), QTime(0, 0), m_tz);
    QList<int> rows;
    for (int row = 0; row < m_events.size(); ++row) {
        const Event &e = m_events.at(row);
        // A zero-length event still belongs to the day it sits on.
        if (e.start < dayEnd && (e.end > dayStart || e.start >= dayStart))
            rows.append(row);
    }
    std::stable_sort(rows.begin(), rows.end(), [this](int a, int b) {
        const Event &x = m_events.at(a);
        const Event &y = m_events.at(b);
        if (x.allDay != y.allDay)
            return x.allDay;
        return x.start < y.start;
    });
    QVariantList list;
    for (int row : std::as_const(rows))
        list.append(eventAt(row));
    return list;
}

QVariantMap EventModel::eventAt(int row) const
{
    QVariantMap event;
    if (row < 0 || row >= m_events.size())
        return event;
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        event.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    return event;
}

int EventModel::rowOf(const QString &uid, const QDateTime &start) const
{
    for (int row = 0; row < m_events.size(); ++row) {
        if (m_events.at(row).uid == uid && m_events.at(row).start == start)
            return row;
    }
    return -1;
}

QString EventModel::timing(const QDateTime &start, const QDateTime &end, const QDateTime &now) const
{
    const auto span = [](qint64 seconds) {
        const qint64 minutes = (seconds + 59) / 60;
        if (minutes < 60)
            return tr("%n min", nullptr, int(minutes));
        return minutes % 60 == 0 ? tr("%n h", nullptr, int(minutes / 60))
                                 : tr("%1 h %2 min").arg(minutes / 60).arg(minutes % 60);
    };
    if (end <= now)
        return tr("Ended");
    if (start <= now)
        return tr("Happening now, %1 left").arg(span(now.secsTo(end)));
    const QDateTime local = start.toTimeZone(m_tz);
    if (local.date() != now.toTimeZone(m_tz).date())
        return {};
    if (now.secsTo(start) < 60 * 60)
        return tr("Starts in %1").arg(span(now.secsTo(start)));
    return tr("Starts at %1").arg(formatClock(local.time(), m_use24Hour));
}

int EventModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_events.size();
}

QVariant EventModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_events.size())
        return {};

    const Event &e = m_events.at(index.row());
    switch (role) {
    case UidRole: return e.uid;
    case SummaryRole: return e.summary;
    case LocationRole: return e.location;
    case ConferenceUrlRole: return e.conferenceUrl;
    case JoinUrlRole: return e.joinUrl();
    case DeclinedRole: return e.declined;
    case CalendarIdRole: return e.calendarId;
    case EventIdRole: return e.eventId;
    case SeriesIdRole: return e.seriesId;
    case ResponseRole: return e.responseStatus;
    case AttendeesRole: return e.attendees;
    case ZoneRole: return e.zone;
    case RecurrenceRole: return e.recurrence;
    case GuestsRole: {
        QVariantList guests;
        for (const Guest &guest : e.guests)
            guests.append(QVariantMap{{u"email"_s, guest.email},
                                      {u"name"_s, guest.name.isEmpty() ? guest.email : guest.name},
                                      {u"response"_s, guest.response},
                                      {u"organizer"_s, guest.organizer},
                                      {u"self"_s, guest.self}});
        return guests;
    }
    case CanEditRole: return e.canEdit;
    case CanRespondRole: return e.canRespond;
    case RecurrenceIdRole: return e.recurrenceId;
    case CalendarColorRole: return e.color;
    case AllDayRole: return e.allDay;
    case StartRole: return e.start;
    case EndRole: return e.end;
    case LaneRole: return e.lane;
    case LaneCountRole: return e.laneCount;
    case DepthRole: return e.depth;
    case DayIndexRole: return static_cast<int>(m_rangeStart.daysTo(e.start.date()));
    case StartMinutesRole: return e.start.time().hour() * 60 + e.start.time().minute();
    case DurationMinutesRole: return static_cast<int>(e.start.secsTo(e.end) / 60);
    case FirstDayRole: return visibleDays(e).first;
    case DaySpanRole: return visibleDays(e).second;
    case CalendarNameRole: return m_calendarNames.value(e.calendarId);
    case DescriptionRole: {
        auto cached = m_plainDescriptions.constFind(index.row());
        if (cached == m_plainDescriptions.cend())
            cached = m_plainDescriptions.insert(index.row(), plainDescription(e.description));
        return *cached;
    }
    default: return {};
    }
}

QHash<int, QByteArray> EventModel::roleNames() const
{
    return {
        {UidRole, "uid"},
        {SummaryRole, "summary"},
        {LocationRole, "location"},
        {ConferenceUrlRole, "conferenceUrl"},
        {JoinUrlRole, "joinUrl"},
        {DeclinedRole, "declined"},
        {CalendarIdRole, "calendarId"},
        {EventIdRole, "eventId"},
        {SeriesIdRole, "seriesId"},
        {ResponseRole, "response"},
        {AttendeesRole, "attendees"},
        {GuestsRole, "guests"},
        {ZoneRole, "zone"},
        {RecurrenceRole, "recurrence"},
        {CanEditRole, "canEdit"},
        {CanRespondRole, "canRespond"},
        {RecurrenceIdRole, "recurrenceId"},
        {CalendarColorRole, "calendarColor"},
        {AllDayRole, "allDay"},
        {DayIndexRole, "dayIndex"},
        {StartMinutesRole, "startMinutes"},
        {DurationMinutesRole, "durationMinutes"},
        {StartRole, "start"},
        {EndRole, "end"},
        {LaneRole, "lane"},
        {LaneCountRole, "laneCount"},
        {DepthRole, "depth"},
        {FirstDayRole, "firstDay"},
        {DaySpanRole, "daySpan"},
        {CalendarNameRole, "calendarName"},
        {DescriptionRole, "description"},
    };
}

} // namespace callie
