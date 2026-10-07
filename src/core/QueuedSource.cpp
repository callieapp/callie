#include "callie/QueuedSource.h"

#include "callie/Logging.h"
#include "callie/Times.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInformation>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

// Waiting changes are tried again this often while the server cannot be reached.
constexpr int kRetryMs = 60 * 1000;

const char *kindName(PendingChange::Kind kind)
{
    switch (kind) {
    case PendingChange::Kind::Create: return "create";
    case PendingChange::Kind::Respond: return "respond";
    case PendingChange::Kind::Delete: return "delete";
    case PendingChange::Kind::Move: return "move";
    }
    return "create";
}

// Times keep their zone, so a move made in one zone is sent in it.
QJsonValue timeJson(const QDateTime &time)
{
    if (!time.isValid())
        return {};
    return QJsonObject{{u"at"_s, time.toString(Qt::ISODateWithMs)},
                       {u"zone"_s, QString::fromUtf8(time.timeZone().id())}};
}

QDateTime timeFrom(const QJsonValue &value)
{
    const QDateTime at = QDateTime::fromString(value[u"at"].toString(), Qt::ISODateWithMs);
    const QTimeZone zone(value[u"zone"].toString().toUtf8());
    return zone.isValid() ? at.toTimeZone(zone) : at;
}

QJsonObject eventJson(const Event &e)
{
    return {{u"uid"_s, e.uid},
            {u"eventId"_s, e.eventId},
            {u"seriesId"_s, e.seriesId},
            {u"calendarId"_s, e.calendarId},
            {u"recurrenceId"_s, timeJson(e.recurrenceId)},
            {u"summary"_s, e.summary},
            {u"location"_s, e.location},
            {u"start"_s, timeJson(e.start)},
            {u"end"_s, timeJson(e.end)},
            {u"allDay"_s, e.allDay},
            {u"zone"_s, e.zone},
            {u"response"_s, e.responseStatus}};
}

Event eventFrom(const QJsonObject &json)
{
    Event e;
    e.uid = json[u"uid"].toString();
    e.eventId = json[u"eventId"].toString();
    e.seriesId = json[u"seriesId"].toString();
    e.calendarId = json[u"calendarId"].toString();
    e.recurrenceId = timeFrom(json[u"recurrenceId"]);
    e.summary = json[u"summary"].toString();
    e.location = json[u"location"].toString();
    e.start = timeFrom(json[u"start"]);
    e.end = timeFrom(json[u"end"]);
    e.allDay = json[u"allDay"].toBool();
    e.zone = json[u"zone"].toString();
    e.responseStatus = json[u"response"].toString();
    return e;
}

QJsonObject changeJson(const PendingChange &c)
{
    return {{u"id"_s, c.id},
            {u"kind"_s, QString::fromLatin1(kindName(c.kind))},
            {u"event"_s, eventJson(c.event)},
            {u"draft"_s, QJsonObject{{u"summary"_s, c.draft.summary},
                                     {u"location"_s, c.draft.location},
                                     {u"start"_s, timeJson(c.draft.start)},
                                     {u"end"_s, timeJson(c.draft.end)},
                                     {u"allDay"_s, c.draft.allDay},
                                     {u"calendarId"_s, c.draft.calendarId},
                                     {u"id"_s, c.draft.id}}},
            {u"status"_s, c.status},
            {u"wholeSeries"_s, c.wholeSeries},
            {u"start"_s, timeJson(c.start)},
            {u"end"_s, timeJson(c.end)},
            {u"notBefore"_s, timeJson(c.notBefore)}};
}

std::optional<PendingChange> changeFrom(const QJsonObject &json)
{
    PendingChange c;
    const QString kind = json[u"kind"].toString();
    if (kind == u"create")
        c.kind = PendingChange::Kind::Create;
    else if (kind == u"respond")
        c.kind = PendingChange::Kind::Respond;
    else if (kind == u"delete")
        c.kind = PendingChange::Kind::Delete;
    else if (kind == u"move")
        c.kind = PendingChange::Kind::Move;
    else
        return std::nullopt;
    c.id = json[u"id"].toString();
    c.event = eventFrom(json[u"event"].toObject());
    const QJsonObject draft = json[u"draft"].toObject();
    c.draft.summary = draft[u"summary"].toString();
    c.draft.location = draft[u"location"].toString();
    c.draft.start = timeFrom(draft[u"start"]);
    c.draft.end = timeFrom(draft[u"end"]);
    c.draft.allDay = draft[u"allDay"].toBool();
    c.draft.calendarId = draft[u"calendarId"].toString();
    c.draft.id = draft[u"id"].toString();
    c.status = json[u"status"].toString();
    c.wholeSeries = json[u"wholeSeries"].toBool();
    c.start = timeFrom(json[u"start"]);
    c.end = timeFrom(json[u"end"]);
    c.notBefore = timeFrom(json[u"notBefore"]);
    return c;
}

bool sameSeries(const Event &e, const PendingChange &c)
{
    return !c.event.seriesId.isEmpty() && e.calendarId == c.event.calendarId &&
           (e.seriesId == c.event.seriesId || e.eventId == c.event.seriesId);
}

bool targets(const Event &e, const PendingChange &c)
{
    if (c.wholeSeries && sameSeries(e, c))
        return true;
    return e.calendarId == c.event.calendarId && e.eventId == c.event.eventId;
}

bool networkUp()
{
    QNetworkInformation *info = QNetworkInformation::instance();
    // With no way to tell, sending is worth a try.
    return !info || info->reachability() != QNetworkInformation::Reachability::Disconnected;
}

} // namespace

QString PendingChange::describe() const
{
    const QString name = kind == Kind::Create ? draft.summary : event.summary;
    switch (kind) {
    case Kind::Create: return QObject::tr("Create %1").arg(name);
    case Kind::Respond: return QObject::tr("Answer %1").arg(name);
    case Kind::Delete: return QObject::tr("Delete %1").arg(name);
    case Kind::Move: return QObject::tr("Move %1").arg(name);
    }
    return name;
}

QueuedSource::QueuedSource(CalendarSource &inner, QString path, QObject *parent)
    : CalendarSource(parent), m_inner(&inner), m_path(std::move(path)),
      m_now([] { return QDateTime::currentDateTimeUtc(); }), m_online(networkUp)
{
    connect(m_inner, &CalendarSource::changed, this, &CalendarSource::changed);
    connect(m_inner, &CalendarSource::statusChanged, this, &CalendarSource::statusChanged);
    connect(m_inner, &CalendarSource::errorOccurred, this, &CalendarSource::errorOccurred);
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &QueuedSource::flush);
    if (QNetworkInformation::loadDefaultBackend()) {
        connect(QNetworkInformation::instance(), &QNetworkInformation::reachabilityChanged, this,
                [this] {
                    if (m_online())
                        flush();
                });
    }
    restore();
    // What waited from last time goes out once the window is up.
    QTimer::singleShot(0, this, &QueuedSource::flush);
}

QString QueuedSource::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
           u"/callie/pending.json"_s;
}

QList<Event> QueuedSource::eventsBetween(const QDateTime &from, const QDateTime &to,
                                         const QTimeZone &tz) const
{
    QList<Event> events = m_inner->eventsBetween(from, to, tz);
    apply(events, m_changes, m_inner->calendars(), from, to, tz);
    return events;
}

QFuture<SourceSnapshot> QueuedSource::load(const QDateTime &from, const QDateTime &to,
                                           const QTimeZone &tz) const
{
    // Copies, since the snapshot may finish on another thread.
    return m_inner->load(from, to, tz)
        .then([changes = m_changes, from, to, tz](SourceSnapshot snapshot) {
            apply(snapshot.events, changes, snapshot.calendars, from, to, tz);
            return snapshot;
        });
}

void QueuedSource::apply(QList<Event> &events, const QList<PendingChange> &changes,
                         const QList<CalendarInfo> &calendars, const QDateTime &from,
                         const QDateTime &to, const QTimeZone &tz)
{
    for (const PendingChange &change : changes) {
        switch (change.kind) {
        case PendingChange::Kind::Create: {
            Event e = change.event;
            if (e.end <= from || e.start >= to)
                break;
            for (const CalendarInfo &calendar : calendars) {
                if (calendar.id == e.calendarId)
                    e.color = calendar.color;
            }
            e.start = e.start.toTimeZone(tz);
            e.end = e.end.toTimeZone(tz);
            events.append(e);
            break;
        }
        case PendingChange::Kind::Delete:
            events.removeIf([&change](const Event &e) { return targets(e, change); });
            break;
        case PendingChange::Kind::Respond:
            for (Event &e : events) {
                if (!targets(e, change))
                    continue;
                e.responseStatus = change.status;
                e.declined = change.status == u"declined";
                for (Guest &guest : e.guests) {
                    if (guest.self)
                        guest.response = change.status;
                }
            }
            break;
        case PendingChange::Kind::Move: {
            // The whole series moves by the shift dragged on the one, on the clock
            // it repeats on, as the server will move it.
            const QTimeZone own(change.event.zone.toUtf8());
            const QTimeZone zone = own.isValid() ? own : change.event.start.timeZone();
            const QDateTime was = change.event.start.toTimeZone(zone);
            const QDateTime now = change.start.toTimeZone(zone);
            const int days = int(was.date().daysTo(now.date()));
            const qint64 secs = was.time().secsTo(now.time());
            const qint64 length = change.start.secsTo(change.end);
            for (Event &e : events) {
                if (!targets(e, change))
                    continue;
                const bool one = e.eventId == change.event.eventId;
                const QDateTime start =
                    one ? change.start : Times::shiftWallClock(e.start, zone, days, secs);
                e.start = start.toTimeZone(tz);
                e.end = start.addSecs(length).toTimeZone(tz);
            }
            break;
        }
        }
    }
}

void QueuedSource::refresh()
{
    flush();
    m_inner->refresh();
}

QString QueuedSource::lastError() const
{
    const QString inner = m_inner->lastError();
    if (m_error.isEmpty())
        return inner;
    return inner.isEmpty() ? m_error : m_error + u'\n' + inner;
}

QStringList QueuedSource::waitingChanges() const
{
    QStringList list;
    for (const PendingChange &change : m_changes)
        list.append(change.describe());
    return list;
}

void QueuedSource::add(PendingChange change, const Created &done)
{
    change.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_changes.append(change);
    save();
    done({});
    Q_EMIT queued(change.id);
    Q_EMIT statusChanged();
    Q_EMIT changed();
    flush();
}

void QueuedSource::createEvent(const EventDraft &draft, Created done)
{
    PendingChange change;
    change.kind = PendingChange::Kind::Create;
    change.draft = draft;
    // Chosen now, in Google's alphabet (0-9, a-v), so a retry after a lost
    // answer finds the event already made instead of making another.
    if (change.draft.id.isEmpty())
        change.draft.id = QUuid::createUuid().toString(QUuid::Id128);
    // Shown at once under a made-up id, until the server gives the real one.
    Event &e = change.event;
    e.uid = u"pending-"_s + QUuid::createUuid().toString(QUuid::WithoutBraces);
    e.eventId = e.uid;
    e.calendarId = draft.calendarId;
    e.summary = draft.summary;
    e.location = draft.location;
    e.start = draft.start;
    e.end = draft.end;
    e.allDay = draft.allDay;
    e.canEdit = true;
    add(change, done);
}

void QueuedSource::respond(const Event &event, const QString &status, bool wholeSeries,
                           Created done)
{
    PendingChange change;
    change.kind = PendingChange::Kind::Respond;
    change.event = event;
    change.status = status;
    change.wholeSeries = wholeSeries;
    add(change, done);
}

void QueuedSource::deleteEvent(const Event &event, bool wholeSeries, Created done)
{
    // An event not created yet is simply not created.
    for (qsizetype i = 0; i < m_changes.size(); ++i) {
        const PendingChange &change = m_changes.at(i);
        if (change.kind == PendingChange::Kind::Create && change.event.eventId == event.eventId &&
            change.id != m_sending) {
            m_changes.removeAt(i);
            save();
            done({});
            Q_EMIT statusChanged();
            Q_EMIT changed();
            return;
        }
    }
    PendingChange change;
    change.kind = PendingChange::Kind::Delete;
    change.event = event;
    change.wholeSeries = wholeSeries;
    change.notBefore = m_now().addSecs(kHoldSecs);
    add(change, done);
}

void QueuedSource::moveEvent(const Event &event, const QDateTime &start, const QDateTime &end,
                             bool wholeSeries, Created done)
{
    // An event not created yet is created at its new times instead.
    for (PendingChange &change : m_changes) {
        if (change.kind == PendingChange::Kind::Create && change.event.eventId == event.eventId &&
            change.id != m_sending) {
            change.draft.start = change.event.start = start;
            change.draft.end = change.event.end = end;
            save();
            done({});
            Q_EMIT changed();
            return;
        }
    }
    PendingChange change;
    change.kind = PendingChange::Kind::Move;
    change.event = event;
    change.start = start;
    change.end = end;
    change.wholeSeries = wholeSeries;
    add(change, done);
}

bool QueuedSource::cancel(const QString &id)
{
    if (id == m_sending)
        return false;
    const qsizetype removed =
        m_changes.removeIf([&id](const PendingChange &change) { return change.id == id; });
    if (removed == 0)
        return false;
    save();
    Q_EMIT statusChanged();
    Q_EMIT changed();
    return true;
}

void QueuedSource::flush()
{
    if (!m_sending.isEmpty() || m_changes.isEmpty())
        return;
    if (!m_online()) {
        m_retry.start(kRetryMs);
        return;
    }
    // In order, so a later change never overtakes the one it builds on.
    const PendingChange &next = m_changes.first();
    if (next.notBefore.isValid() && next.notBefore > m_now()) {
        m_retry.start(int(m_now().msecsTo(next.notBefore)) + 50);
        return;
    }
    send(next);
}

void QueuedSource::send(const PendingChange &change)
{
    m_sending = change.id;
    const QPointer<QueuedSource> self(this);
    const QString id = change.id;
    const auto done = [self, id](const Outcome &outcome) {
        if (self)
            self->finished(id, outcome);
    };
    switch (change.kind) {
    case PendingChange::Kind::Create: m_inner->createEvent(change.draft, done); break;
    case PendingChange::Kind::Respond:
        m_inner->respond(change.event, change.status, change.wholeSeries, done);
        break;
    case PendingChange::Kind::Delete:
        m_inner->deleteEvent(change.event, change.wholeSeries, done);
        break;
    case PendingChange::Kind::Move:
        m_inner->moveEvent(change.event, change.start, change.end, change.wholeSeries, done);
        break;
    }
}

void QueuedSource::finished(const QString &id, const Outcome &outcome)
{
    m_sending.clear();
    const QString &error = outcome.error;
    if (!error.isEmpty() && (outcome.retry || !m_online())) {
        // Something that can pass, such as the network: it waits for the next try.
        qCInfo(lcSync) << "keeping a change for later:" << error;
        Q_EMIT statusChanged();
        m_retry.start(kRetryMs);
        return;
    }
    const auto found = std::find_if(m_changes.cbegin(), m_changes.cend(),
                                    [&id](const PendingChange &c) { return c.id == id; });
    if (found != m_changes.cend()) {
        if (!error.isEmpty()) {
            m_error = QStringLiteral("%1: %2").arg(found->describe(), error);
            Q_EMIT errorOccurred(m_error);
        } else {
            m_error.clear();
        }
        m_changes.erase(found);
        save();
    }
    Q_EMIT statusChanged();
    Q_EMIT changed();
    flush();
}

void QueuedSource::save() const
{
    if (m_changes.isEmpty()) {
        QFile::remove(m_path);
        return;
    }
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcSync) << "could not save waiting changes:" << file.errorString();
        return;
    }
    QJsonArray list;
    for (const PendingChange &change : m_changes)
        list.append(changeJson(change));
    file.write(QJsonDocument(list).toJson(QJsonDocument::Compact));
    if (!file.commit())
        qCWarning(lcSync) << "could not save waiting changes:" << file.errorString();
}

void QueuedSource::restore()
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    for (const QJsonValue &value : QJsonDocument::fromJson(file.readAll()).array()) {
        if (const auto change = changeFrom(value.toObject()))
            m_changes.append(*change);
    }
}

} // namespace callie
