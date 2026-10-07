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
    case PendingChange::Kind::Update: return "update";
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

// Only what an edit sets is written, so what it leaves alone stays unset.
QJsonObject editJson(const EventEdit &edit)
{
    QJsonObject json;
    if (edit.summary)
        json.insert(u"summary"_s, *edit.summary);
    if (edit.location)
        json.insert(u"location"_s, *edit.location);
    if (edit.description)
        json.insert(u"description"_s, *edit.description);
    if (edit.start)
        json.insert(u"start"_s, timeJson(*edit.start));
    if (edit.end)
        json.insert(u"end"_s, timeJson(*edit.end));
    if (edit.allDay)
        json.insert(u"allDay"_s, *edit.allDay);
    if (edit.recurrence)
        json.insert(u"recurrence"_s, QJsonArray::fromStringList(*edit.recurrence));
    if (edit.guests)
        json.insert(u"guests"_s, QJsonArray::fromStringList(*edit.guests));
    if (edit.videoCall)
        json.insert(u"videoCall"_s, *edit.videoCall);
    return json;
}

EventEdit editFrom(const QJsonObject &json)
{
    const auto strings = [](const QJsonValue &value) {
        QStringList list;
        for (const QJsonValue &item : value.toArray())
            list.append(item.toString());
        return list;
    };
    EventEdit edit;
    if (json.contains(u"summary"))
        edit.summary = json[u"summary"].toString();
    if (json.contains(u"location"))
        edit.location = json[u"location"].toString();
    if (json.contains(u"description"))
        edit.description = json[u"description"].toString();
    if (json.contains(u"start"))
        edit.start = timeFrom(json[u"start"]);
    if (json.contains(u"end"))
        edit.end = timeFrom(json[u"end"]);
    if (json.contains(u"allDay"))
        edit.allDay = json[u"allDay"].toBool();
    if (json.contains(u"recurrence"))
        edit.recurrence = strings(json[u"recurrence"]);
    if (json.contains(u"guests"))
        edit.guests = strings(json[u"guests"]);
    if (json.contains(u"videoCall"))
        edit.videoCall = json[u"videoCall"].toBool();
    return edit;
}

QJsonObject changeJson(const PendingChange &c)
{
    return {{u"id"_s, c.id},
            {u"kind"_s, QString::fromLatin1(kindName(c.kind))},
            {u"event"_s, eventJson(c.event)},
            {u"draft"_s, QJsonObject{{u"summary"_s, c.draft.summary},
                                     {u"location"_s, c.draft.location},
                                     {u"description"_s, c.draft.description},
                                     {u"start"_s, timeJson(c.draft.start)},
                                     {u"end"_s, timeJson(c.draft.end)},
                                     {u"allDay"_s, c.draft.allDay},
                                     {u"calendarId"_s, c.draft.calendarId},
                                     {u"id"_s, c.draft.id}}},
            {u"status"_s, c.status},
            {u"wholeSeries"_s, c.wholeSeries},
            {u"start"_s, timeJson(c.start)},
            {u"end"_s, timeJson(c.end)},
            {u"notBefore"_s, timeJson(c.notBefore)},
            {u"edit"_s, editJson(c.edit)},
            {u"scope"_s, int(c.scope)}};
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
    else if (kind == u"update")
        c.kind = PendingChange::Kind::Update;
    else
        return std::nullopt;
    c.id = json[u"id"].toString();
    c.event = eventFrom(json[u"event"].toObject());
    const QJsonObject draft = json[u"draft"].toObject();
    c.draft.summary = draft[u"summary"].toString();
    c.draft.location = draft[u"location"].toString();
    c.draft.description = draft[u"description"].toString();
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
    c.edit = editFrom(json[u"edit"].toObject());
    c.scope = EditScope(std::clamp(json[u"scope"].toInt(), 0, int(EditScope::AllEvents)));
    return c;
}

bool sameSeries(const Event &e, const PendingChange &c)
{
    return !c.event.seriesId.isEmpty() && e.calendarId == c.event.calendarId &&
           (e.seriesId == c.event.seriesId || e.eventId == c.event.seriesId);
}

// Where an occurrence sat in its series before any move.
QDateTime originalStart(const Event &e)
{
    return e.recurrenceId.isValid() ? e.recurrenceId : e.start;
}

bool targets(const Event &e, const PendingChange &c)
{
    if (c.wholeSeries && sameSeries(e, c))
        return true;
    if (c.kind == PendingChange::Kind::Update && c.scope == EditScope::ThisAndFollowing &&
        sameSeries(e, c))
        return originalStart(e) >= originalStart(c.event);
    return e.calendarId == c.event.calendarId && e.eventId == c.event.eventId;
}

// What a change of `kind` to `event` says once done, such as "Moved Standup".
QString doneText(PendingChange::Kind kind, const Event &event)
{
    PendingChange change;
    change.kind = kind;
    change.event = event;
    return change.describeDone();
}

bool networkUp()
{
    QNetworkInformation *info = QNetworkInformation::instance();
    // With no way to tell, sending is worth a try.
    return !info || info->reachability() != QNetworkInformation::Reachability::Disconnected;
}

} // namespace

QString PendingChange::describeDone() const
{
    const QString name = kind == Kind::Create ? draft.summary : event.summary;
    switch (kind) {
    case Kind::Create: return QObject::tr("Created %1").arg(name);
    case Kind::Respond: return QObject::tr("Answered %1").arg(name);
    case Kind::Delete: return QObject::tr("Deleted %1").arg(name);
    case Kind::Move: return QObject::tr("Moved %1").arg(name);
    case Kind::Update: return QObject::tr("Changed %1").arg(name);
    }
    return name;
}

QString PendingChange::describe() const
{
    const QString name = kind == Kind::Create ? draft.summary : event.summary;
    switch (kind) {
    case Kind::Create: return QObject::tr("Create %1").arg(name);
    case Kind::Respond: return QObject::tr("Answer %1").arg(name);
    case Kind::Delete: return QObject::tr("Delete %1").arg(name);
    case Kind::Move: return QObject::tr("Move %1").arg(name);
    case Kind::Update: return QObject::tr("Change %1").arg(name);
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
        case PendingChange::Kind::Update:
            for (Event &e : events) {
                if (targets(e, change))
                    applyEdit(e, change.event, change.edit);
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
                if (e.allDay) {
                    // Whole days, however long the clock makes them.
                    const qint64 span = change.start.date().daysTo(change.end.date());
                    e.start = QDateTime(start.toTimeZone(tz).date(), QTime(0, 0), tz);
                    e.end = QDateTime(e.start.date().addDays(span), QTime(0, 0), tz);
                    continue;
                }
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
    // Those still held back can be undone and are not waiting on anything yet.
    QStringList list;
    const QDateTime now = m_now();
    for (const PendingChange &change : m_changes) {
        if (!change.notBefore.isValid() || change.notBefore <= now)
            list.append(change.describe());
    }
    return list;
}

void QueuedSource::add(PendingChange change, const Created &done)
{
    change.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    change.notBefore = m_now().addSecs(kHoldSecs);
    m_changes.append(change);
    save();
    done({});
    Q_EMIT changeMade(change.id, change.describeDone());
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
    e.description = draft.description;
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
            const PendingChange before = change;
            m_changes.removeAt(i);
            save();
            done({});
            folded(i, before, doneText(PendingChange::Kind::Delete, event));
            Q_EMIT statusChanged();
            Q_EMIT changed();
            return;
        }
    }
    PendingChange change;
    change.kind = PendingChange::Kind::Delete;
    change.event = event;
    change.wholeSeries = wholeSeries;
    add(change, done);
}

void QueuedSource::moveEvent(const Event &event, const QDateTime &start, const QDateTime &end,
                             bool wholeSeries, Created done)
{
    // An event not created yet is created at its new times instead.
    for (qsizetype i = 0; i < m_changes.size(); ++i) {
        PendingChange &change = m_changes[i];
        if (change.kind == PendingChange::Kind::Create && change.event.eventId == event.eventId &&
            change.id != m_sending) {
            const PendingChange before = change;
            change.draft.start = change.event.start = start;
            change.draft.end = change.event.end = end;
            // Held again, so the undo the toast offers for this still works.
            change.notBefore = std::max(change.notBefore, m_now().addSecs(kHoldSecs));
            save();
            done({});
            folded(i, before, doneText(PendingChange::Kind::Move, event));
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

void QueuedSource::folded(qsizetype index, const PendingChange &before, const QString &what)
{
    // Only the latest few can still be undone from the toast.
    constexpr qsizetype kKept = 16;
    if (m_folded.size() >= kKept)
        m_folded.clear();
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_folded.insert(id, {index, before});
    Q_EMIT changeMade(id, what);
}

void QueuedSource::updateEvent(const Event &event, const EventEdit &edit, EditScope scope,
                               Created done)
{
    // An event not created yet is created as edited, if a creation can say it all.
    for (qsizetype i = 0; i < m_changes.size(); ++i) {
        PendingChange &change = m_changes[i];
        if (change.kind != PendingChange::Kind::Create || change.event.eventId != event.eventId ||
            change.id == m_sending)
            continue;
        if (edit.recurrence || edit.guests || edit.videoCall) {
            done(tr("Wait a moment for the new event to be saved, then change it."));
            return;
        }
        const PendingChange before = change;
        applyEdit(change.event, event, edit);
        change.draft.summary = change.event.summary;
        change.draft.location = change.event.location;
        change.draft.description = change.event.description;
        change.draft.start = change.event.start;
        change.draft.end = change.event.end;
        change.draft.allDay = change.event.allDay;
        save();
        done({});
        folded(i, before, doneText(PendingChange::Kind::Update, event));
        Q_EMIT changed();
        return;
    }
    PendingChange change;
    change.kind = PendingChange::Kind::Update;
    change.event = event;
    change.edit = edit;
    change.scope = scope;
    change.wholeSeries = scope == EditScope::AllEvents;
    add(change, done);
}

bool QueuedSource::undoChange(const QString &id)
{
    if (const auto folded = m_folded.constFind(id); folded != m_folded.cend()) {
        // The creation goes back to how it was, unless it has gone out since.
        const auto [index, before] = *folded;
        m_folded.erase(folded);
        if (before.id == m_sending)
            return false;
        const auto current =
            std::find_if(m_changes.begin(), m_changes.end(),
                         [&before](const PendingChange &c) { return c.id == before.id; });
        if (current != m_changes.end())
            *current = before;
        else
            m_changes.insert(std::min(index, m_changes.size()), before);
        save();
        Q_EMIT statusChanged();
        Q_EMIT changed();
        return true;
    }
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
    // In order, so a later change never overtakes the one it builds on.
    const PendingChange &next = m_changes.first();
    if (next.notBefore.isValid() && next.notBefore > m_now()) {
        m_retry.start(int(m_now().msecsTo(next.notBefore)) + 50);
        return;
    }
    if (!m_online()) {
        // Now waiting on the network rather than on its hold.
        Q_EMIT statusChanged();
        m_retry.start(kRetryMs);
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
    case PendingChange::Kind::Update:
        m_inner->updateEvent(change.event, change.edit, change.scope, done);
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
    // Gone out or dropped, so what folded into it can no longer be undone.
    m_folded.removeIf([&id](const auto &entry) { return entry.value().second.id == id; });
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
    if (m_path.isEmpty())
        return;
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
    if (m_path.isEmpty())
        return;
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    for (const QJsonValue &value : QJsonDocument::fromJson(file.readAll()).array()) {
        if (const auto change = changeFrom(value.toObject()))
            m_changes.append(*change);
    }
}

} // namespace callie
