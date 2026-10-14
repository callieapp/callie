#include "callie/MicrosoftSource.h"

#include "callie/GraphEvents.h"
#include "callie/Identity.h"
#include "callie/Logging.h"
#include "callie/MicrosoftTokenProvider.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

const QString kStateFile = u".callie-graph.json"_s;
const QString kAccountFile = u".callie-account.json"_s;
/// Outlook's versions of the files, beside them, to tell what changed here.
const QString kCopies = u".callie-graph"_s;
const QString kFields =
    u"id,iCalUId,type,changeKey,lastModifiedDateTime,subject,body,location,start,end,isAllDay,"
    "showAs,isCancelled,isOrganizer,organizer,attendees,onlineMeeting,recurrence,"
    "originalStartTimeZone,originalStart,responseStatus,seriesMasterId"_s;

QString slug(const QString &text)
{
    QString name = text.toLower();
    name.replace(QRegularExpression(u"[^\\w.@-]+"_s), u"-"_s);
    name.remove(QRegularExpression(u"^[-.]+|-+$"_s));
    return name.isEmpty() ? u"calendar"_s : name;
}

/// A file name for an event, from its UID when that makes a safe one.
QString nameFor(const QString &uid)
{
    static const QRegularExpression safe(u"^[A-Za-z0-9._@-]{1,200}$"_s);
    return (safe.match(uid).hasMatch() && !uid.startsWith(u'.')
                ? uid
                : QString::fromLatin1(
                      QCryptographicHash::hash(uid.toUtf8(), QCryptographicHash::Sha1).toHex())) +
           u".ics"_s;
}

QString encoded(const QString &id)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(id));
}

bool writeJson(const QString &path, const QJsonObject &json)
{
    QSaveFile file(path);
    const QByteArray data = QJsonDocument(json).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object()
                                          : QJsonObject();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void writeMeta(const QString &path, const QString &value)
{
    QSaveFile file(path);
    const QByteArray data = value.toUtf8() + '\n';
    if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size())
        file.commit();
}

KCalendarCore::Event::Ptr masterOf(const ics::Group &group)
{
    const auto found =
        std::ranges::find_if(group, [](const auto &e) { return !e->hasRecurrenceId(); });
    return found == group.cend() ? KCalendarCore::Event::Ptr() : *found;
}

KCalendarCore::Event::Ptr occurrenceOf(const ics::Group &group, const QDateTime &id)
{
    const auto found = std::ranges::find_if(
        group, [&id](const auto &e) { return e->hasRecurrenceId() && e->recurrenceId() == id; });
    return found == group.cend() ? KCalendarCore::Event::Ptr() : *found;
}

/// The account's own answer to the event, as an attendee.
std::optional<KCalendarCore::Attendee::PartStat> answerOf(const KCalendarCore::Event &event,
                                                          const QString &self)
{
    for (const KCalendarCore::Attendee &attendee : event.attendees()) {
        if (attendee.email().compare(self, Qt::CaseInsensitive) == 0)
            return attendee.status();
    }
    return std::nullopt;
}

/// Graph's action for an answer, as in me/events/{id}/accept.
QString actionFor(KCalendarCore::Attendee::PartStat answer)
{
    using A = KCalendarCore::Attendee;
    return answer == A::Accepted    ? u"accept"_s
           : answer == A::Declined  ? u"decline"_s
           : answer == A::Tentative ? u"tentativelyAccept"_s
                                    : QString();
}

/// Offline, signed out, throttled or failing: kept for the next sync.
bool later(const GraphError &error)
{
    return error.later() || error.unauthorized();
}

/// What the occurrences of a series need: patched, deleted or answered.
struct Step
{
    enum Kind { Patch, Delete, Answer } kind;
    /// The occurrence's original start; invalid for the series itself.
    QDateTime occurrence;
    bool allDay = false;
    QJsonObject fields;
    QString action;
};

QList<Step> stepsBetween(const ics::Group &before, const ics::Group &after, bool organizer,
                         const QString &self)
{
    QList<Step> steps;
    const auto oldMaster = masterOf(before);
    const auto newMaster = masterOf(after);
    const bool allDay = newMaster && newMaster->allDay();
    const auto answered = [&self](const auto &was, const auto &now) -> QString {
        const auto old = was ? answerOf(*was, self) : std::nullopt;
        const auto answer = answerOf(*now, self);
        return answer && answer != old ? actionFor(*answer) : QString();
    };
    if (!organizer && newMaster) {
        if (const QString action = answered(oldMaster, newMaster); !action.isEmpty())
            steps << Step{Step::Answer, {}, false, {}, action};
    }
    for (const auto &event : after) {
        if (!event->hasRecurrenceId())
            continue;
        const auto was = occurrenceOf(before, event->recurrenceId());
        if (!organizer) {
            if (const QString action = answered(was, event); !action.isEmpty())
                steps << Step{Step::Answer, event->recurrenceId(), allDay, {}, action};
            continue;
        }
        if (event->status() == KCalendarCore::Incidence::StatusCanceled) {
            if (!was || was->status() != KCalendarCore::Incidence::StatusCanceled)
                steps << Step{Step::Delete, event->recurrenceId(), allDay, {}, {}};
            continue;
        }
        QString error;
        auto fields = graph::fieldsFor(*event, &error);
        const auto old = was ? graph::fieldsFor(*was, &error) : std::nullopt;
        if (fields && fields != old) {
            // Who comes is the series' to say.
            fields->remove(u"attendees"_s);
            steps << Step{Step::Patch, event->recurrenceId(), allDay, *fields, {}};
        }
    }
    if (organizer && newMaster && newMaster->recurs()) {
        const auto skipped = [](const auto &master) {
            QList<QDateTime> days;
            if (!master)
                return days;
            for (const QDateTime &at : master->recurrence()->exDateTimes())
                days << at;
            for (const QDate &day : master->recurrence()->exDates())
                days << day.startOfDay();
            return days;
        };
        const QList<QDateTime> had = skipped(oldMaster);
        for (const QDateTime &day : skipped(newMaster)) {
            if (!had.contains(day))
                steps << Step{Step::Delete, day, allDay, {}, {}};
        }
    }
    return steps;
}

} // namespace

MicrosoftSource::MicrosoftSource(QString root, GraphApi *graph, MicrosoftTokenProvider *tokens,
                                 QObject *parent)
    : VdirSource(parent), m_root(std::move(root)), m_graph(graph), m_tokens(tokens)
{
    m_soon.setSingleShot(true);
    m_soon.setInterval(500);
    connect(&m_soon, &QTimer::timeout, this, &MicrosoftSource::refresh);
}

MicrosoftSource::~MicrosoftSource() = default;

QString MicrosoftSource::defaultRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u'/' +
           identity::dirName() + u"/microsoft"_s;
}

QString MicrosoftSource::accountPath(const QString &accountId) const
{
    return m_root + u'/' + slug(accountId);
}

bool MicrosoftSource::hasAccount(const QString &accountId) const
{
    return m_accounts.contains(accountId);
}

void MicrosoftSource::setAccounts(const QStringList &accounts)
{
    m_accounts = accounts;
    for (const QString &id : QSet<QString>(m_running)) {
        if (!hasAccount(id))
            finish(id, {});
    }
    scan();
}

void MicrosoftSource::scan()
{
    QList<Folder> folders;
    for (const QString &account : std::as_const(m_accounts)) {
        const QDir dir(accountPath(account));
        for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString folder = dir.filePath(name);
            folders.append({u"microsoft:"_s + slug(account) + u'/' + name, folder,
                            readState(folder).writable, account, account});
        }
    }
    const auto same = [](const Folder &a, const Folder &b) {
        return a.id == b.id && a.writable == b.writable && a.self == b.self;
    };
    if (!std::ranges::equal(folders, this->folders(), same))
        setFolders(folders);
}

QList<CalendarInfo> MicrosoftSource::calendars() const
{
    QList<CalendarInfo> list = VdirSource::calendars();
    const QList<Folder> all = folders();
    for (CalendarInfo &calendar : list) {
        const auto folder = std::ranges::find(all, calendar.id, &Folder::id);
        calendar.videoCalls = folder != all.cend() && calendar.writable &&
                              !readState(folder->path).meetings.isEmpty();
    }
    return list;
}

bool MicrosoftSource::forget(const QString &accountId, QString *error)
{
    const QString path = accountPath(accountId);
    if (QDir(path).exists() && !QDir(path).removeRecursively()) {
        *error = tr("Callie could not delete %1.").arg(path);
        return false;
    }
    m_accounts.removeAll(accountId);
    m_running.remove(accountId);
    m_again.remove(accountId);
    scan();
    return true;
}

bool MicrosoftSource::resync(const QString &accountId, QString *error)
{
    if (!hasAccount(accountId)) {
        *error = tr("There is no Microsoft account %1.").arg(accountId);
        return false;
    }
    const QString path = accountPath(accountId);
    if (QDir(path).exists() && !QDir(path).removeRecursively()) {
        *error = tr("Callie could not delete %1.").arg(path);
        return false;
    }
    scan();
    sync(accountId);
    return true;
}

void MicrosoftSource::refresh()
{
    scan();
    for (const QString &account : std::as_const(m_accounts))
        sync(account);
    VdirSource::refresh();
}

void MicrosoftSource::updateEvent(const Event &event, const EventEdit &edit, EditScope scope,
                                  Created done)
{
    if (!edit.videoCall) {
        VdirSource::updateEvent(event, edit, scope, std::move(done));
        return;
    }
    const QList<Folder> all = folders();
    const auto folder = std::ranges::find(all, event.calendarId, &Folder::id);
    if (folder == all.cend()) {
        done(tr("That calendar is not connected any more."));
        return;
    }
    State state = readState(folder->path);
    if (!*edit.videoCall && event.conferenceUrl.isValid()) {
        done(tr("Outlook keeps a meeting's Teams link once it has one."));
        return;
    }
    if (*edit.videoCall && !event.conferenceUrl.isValid()) {
        if (state.meetings.isEmpty()) {
            done(tr("This calendar cannot add online meetings."));
            return;
        }
        const QString file = fileOf(*folder, event.uid);
        if (file.isEmpty()) {
            done(tr("That event is not in the calendar any more."));
            return;
        }
        state.teams.insert(file);
        writeState(folder->path, state);
        m_soon.start();
        Q_EMIT statusChanged();
    }
    EventEdit rest = edit;
    rest.videoCall.reset();
    if (rest.isEmpty()) {
        done({});
        return;
    }
    VdirSource::updateEvent(event, rest, scope, std::move(done));
}

void MicrosoftSource::fileChanged(const Folder &folder, const QString &fileName, bool removed)
{
    State state = readState(folder.path);
    state.pending.insert(fileName, {removed, ++state.seq});
    writeState(folder.path, state);
    m_soon.start();
    Q_EMIT statusChanged();
}

void MicrosoftSource::withToken(const QString &accountId, Call call, const Done &failed,
                                bool retried)
{
    if (!m_graph || !m_tokens) {
        failed(tr("Callie is not online."));
        return;
    }
    const QPointer<MicrosoftSource> self(this);
    const Account account{u"microsoft"_s, accountId};
    m_tokens->accessToken(account, [self, accountId, account, call, failed,
                                    retried](const QString &token, const QString &error) {
        if (!self)
            return;
        if (!error.isEmpty()) {
            failed(error);
            return;
        }
        call(token, [self, accountId, account, call, failed, retried] {
            if (!self)
                return;
            if (retried) {
                failed(tr("Microsoft turned down the sign-in; connect the account again."));
                return;
            }
            self->m_tokens->invalidate(account);
            self->withToken(accountId, call, failed, true);
        });
    });
}

void MicrosoftSource::sync(const QString &accountId)
{
    if (m_running.contains(accountId)) {
        m_again.insert(accountId);
        return;
    }
    if (!m_graph || !m_tokens)
        return;
    m_running.insert(accountId);
    Q_EMIT statusChanged();
    const QPointer<MicrosoftSource> self(this);
    withToken(
        accountId,
        [self, accountId](const QString &token, const std::function<void()> &retry) {
            self->m_graph->list(
                token,
                u"me/calendars?$select=id,name,hexColor,canEdit,allowedOnlineMeetingProviders,"
                "defaultOnlineMeetingProvider"_s,
                [self, accountId, token, retry](const QList<QJsonObject> &calendars,
                                                const GraphError &error) {
                    if (!self || !self->m_running.contains(accountId))
                        return;
                    if (error.unauthorized()) {
                        retry();
                        return;
                    }
                    if (error) {
                        self->finish(accountId, error.message);
                        return;
                    }
                    const QStringList problems = self->updateCalendars(accountId, calendars);
                    if (!self->m_contactsRead.contains(accountId))
                        self->readContacts(accountId, token);
                    QList<Folder> mine;
                    for (const Folder &folder : self->folders()) {
                        if (folder.account == accountId)
                            mine.append(folder);
                    }
                    self->syncFolders(accountId, token, mine, problems);
                });
        },
        [self, accountId](const QString &error) {
            if (self)
                self->finish(accountId, error);
        });
}

void MicrosoftSource::finish(const QString &accountId, const QString &error)
{
    const QString path = accountPath(accountId);
    AccountState state = readAccount(path);
    state.error = error;
    if (error.isEmpty())
        state.synced = QDateTime::currentDateTimeUtc();
    else
        qCInfo(lcSync) << "Microsoft" << accountId << "not synced:" << error;
    if (hasAccount(accountId) && QDir().mkpath(path))
        writeAccount(path, state);
    m_running.remove(accountId);
    Q_EMIT statusChanged();
    if (m_again.remove(accountId) && hasAccount(accountId))
        sync(accountId);
    if (m_running.isEmpty())
        Q_EMIT synced();
}

QStringList MicrosoftSource::updateCalendars(const QString &accountId,
                                             const QList<QJsonObject> &calendars)
{
    const QString path = accountPath(accountId);
    const QDir dir(path);
    QHash<QString, QString> byId;
    for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        byId.insert(readState(dir.filePath(name)).calendarId, dir.filePath(name));
    QSet<QString> seen;
    for (const QJsonObject &calendar : calendars) {
        const QString id = calendar[u"id"].toString();
        QString folder = byId.value(id);
        if (folder.isEmpty()) {
            const QString base = slug(calendar[u"name"].toString());
            QString name = base;
            for (int n = 2; dir.exists(name); ++n)
                name = base + u'-' + QString::number(n);
            folder = dir.filePath(name);
            QDir().mkpath(folder);
        }
        seen.insert(folder);
        State state = readState(folder);
        state.calendarId = id;
        state.writable = calendar[u"canEdit"].toBool(true);
        // Teams where the account has it, else whatever Outlook offers.
        const QString preferred = calendar[u"defaultOnlineMeetingProvider"].toString();
        state.meetings = preferred != u"unknown" ? preferred : QString();
        if (state.meetings.isEmpty()) {
            for (const QJsonValue &allowed : calendar[u"allowedOnlineMeetingProviders"].toArray()) {
                if (allowed.toString() != u"unknown") {
                    state.meetings = allowed.toString();
                    break;
                }
            }
        }
        writeState(folder, state);
        writeMeta(folder + u"/displayname"_s, calendar[u"name"].toString());
        const QColor color = QColor::fromString(calendar[u"hexColor"].toString());
        if (color.isValid())
            writeMeta(folder + u"/color"_s, color.name());
    }
    // Calendars gone from Outlook go from here too, unless changes wait in them.
    QStringList problems;
    for (const QString &folder : std::as_const(byId)) {
        if (seen.contains(folder))
            continue;
        if (readState(folder).pending.isEmpty()) {
            QDir(folder).removeRecursively();
            continue;
        }
        problems << tr("%1 is gone from Outlook, with changes not sent; they are kept in %2.")
                        .arg(readFile(folder + u"/displayname"_s).trimmed(), folder);
    }
    scan();
    return problems;
}

void MicrosoftSource::syncFolders(const QString &accountId, const QString &token,
                                  QList<Folder> folders, QStringList errors)
{
    if (folders.isEmpty() || !m_running.contains(accountId)) {
        finish(accountId, errors.join(u'\n'));
        return;
    }
    const Folder folder = folders.takeFirst();
    const QPointer<MicrosoftSource> self(this);
    push(token, folder, [self, accountId, token, folder, folders, errors](const QString &pushed) {
        if (!self)
            return;
        self->pull(
            token, folder,
            [self, accountId, token, folders, errors, pushed](const QString &pulled) mutable {
                if (!self)
                    return;
                for (const QString &error : {pushed, pulled}) {
                    if (!error.isEmpty() && !errors.contains(error))
                        errors << error;
                }
                self->syncFolders(accountId, token, folders, errors);
            });
    });
}

void MicrosoftSource::pull(const QString &token, const Folder &folder, Done done)
{
    const QString calendarId = readState(folder.path).calendarId;
    const QPointer<MicrosoftSource> self(this);
    m_graph->list(
        token, u"me/calendars/%1/events?$select=%2&$top=250"_s.arg(encoded(calendarId), kFields),
        [self, token, folder, done](const QList<QJsonObject> &items, const GraphError &error) {
            if (!self)
                return;
            if (error) {
                done(error.message);
                return;
            }
            State state = readState(folder.path);
            QHash<QString, QString> fileOf;
            for (auto it = state.files.cbegin(); it != state.files.cend(); ++it)
                fileOf.insert(it->id, it.key());
            QSet<QString> seen;
            QStringList series;
            QString problem;
            bool changed = false;
            for (const QJsonObject &item : items) {
                const QString id = item[u"id"].toString();
                seen.insert(id);
                const QString fileName = fileOf.value(id, nameFor(item[u"iCalUId"].toString(id)));
                const Remote remote = state.files.value(fileName);
                // Changes not sent yet stay as they are until they go.
                if (state.pending.contains(fileName) ||
                    (remote.id == id && remote.changeKey == item[u"changeKey"].toString() &&
                     QFile::exists(folder.path + u'/' + fileName)))
                    continue;
                if (item[u"type"].toString() == u"seriesMaster") {
                    series << fileName << id;
                    continue;
                }
                const QByteArray data = ics::write(graph::toIcs({item, {}, {}}));
                if (self->writeFile(folder, fileName, data, &problem)) {
                    keepServerCopy(folder, fileName, data);
                    state.files.insert(fileName, {id, item[u"changeKey"].toString(),
                                                  item[u"isOrganizer"].toBool(true)});
                    changed = true;
                }
            }
            for (auto it = state.files.begin(); it != state.files.end();) {
                if (seen.contains(it->id) || state.pending.contains(it.key())) {
                    ++it;
                    continue;
                }
                self->removeFile(folder, it.key(), &problem);
                QFile::remove(folder.path + u'/' + kCopies + u'/' + it.key());
                it = state.files.erase(it);
                changed = true;
            }
            writeState(folder.path, state);
            if (changed)
                Q_EMIT self->changed();
            self->fetchSeries(token, folder, series, [done, problem](const QString &error) {
                done(error.isEmpty() ? problem : error);
            });
        });
}

void MicrosoftSource::fetchSeries(const QString &token, const Folder &folder, QStringList ids,
                                  Done done)
{
    if (ids.size() < 2) {
        done({});
        return;
    }
    const QString fileName = ids.takeFirst();
    const QString id = ids.takeFirst();
    const QPointer<MicrosoftSource> self(this);
    store(token, folder, fileName, id, [self, token, folder, ids, done](const QString &error) {
        if (!self)
            return;
        if (!error.isEmpty())
            done(error);
        else
            self->fetchSeries(token, folder, ids, done);
    });
}

void MicrosoftSource::store(const QString &token, const Folder &folder, const QString &fileName,
                            const QString &id, Done done)
{
    const QPointer<MicrosoftSource> self(this);
    m_graph->send(
        token, "GET",
        u"me/events/%1?$select=%2,cancelledOccurrences&$expand=exceptionOccurrences($select=%2)"_s
            .arg(encoded(id), kFields),
        {}, [self, folder, fileName, done](const QJsonObject &json, const GraphError &error) {
            if (!self)
                return;
            State state = readState(folder.path);
            QString problem;
            if (error.status == 404) {
                self->removeFile(folder, fileName, &problem);
                QFile::remove(folder.path + u'/' + kCopies + u'/' + fileName);
                state.files.remove(fileName);
                writeState(folder.path, state);
                Q_EMIT self->changed();
                done(problem);
                return;
            }
            if (error) {
                done(error.message);
                return;
            }
            if (state.pending.contains(fileName)) {
                done({});
                return;
            }
            graph::Series series{json, {}, {}};
            for (const QJsonValue &exception : json[u"exceptionOccurrences"].toArray())
                series.exceptions << exception.toObject();
            for (const QJsonValue &cancelled : json[u"cancelledOccurrences"].toArray())
                series.cancelled << cancelled.toString();
            const QByteArray data = ics::write(graph::toIcs(series));
            if (self->writeFile(folder, fileName, data, &problem)) {
                keepServerCopy(folder, fileName, data);
                state.files.insert(fileName, {json[u"id"].toString(), json[u"changeKey"].toString(),
                                              json[u"isOrganizer"].toBool(true)});
                writeState(folder.path, state);
                Q_EMIT self->changed();
            }
            done(problem);
        });
}

void MicrosoftSource::push(const QString &token, const Folder &folder, Done done)
{
    const State state = readState(folder.path);
    QStringList waiting = state.pending.keys();
    // Files waiting only for a Teams link go too.
    for (const QString &file : state.teams) {
        if (!waiting.contains(file))
            waiting << file;
    }
    if (waiting.isEmpty()) {
        done({});
        return;
    }
    const QString fileName = waiting.first();
    send(token, folder, fileName, state.pending.value(fileName), done);
}

void MicrosoftSource::send(const QString &token, const Folder &folder, const QString &fileName,
                           const Waiting &waiting, Done done)
{
    const State state = readState(folder.path);
    const Remote remote = state.files.value(fileName);
    const QPointer<MicrosoftSource> self(this);
    // Sent, or settled: the change is done unless a newer one came meanwhile.
    const auto settled = [self, token, folder, fileName, waiting, done] {
        if (!self)
            return;
        State state = readState(folder.path);
        if (state.pending.value(fileName).seq == waiting.seq)
            state.pending.remove(fileName);
        state.teams.remove(fileName);
        writeState(folder.path, state);
        Q_EMIT self->statusChanged();
        self->push(token, folder, done);
    };
    // Turned down: Outlook's version stands, and the user hears why.
    const auto refused = [self, token, folder, fileName, remote, settled](const QString &why) {
        if (!self)
            return;
        Q_EMIT self->errorOccurred(why);
        State state = readState(folder.path);
        state.pending.remove(fileName);
        writeState(folder.path, state);
        if (remote.id.isEmpty()) {
            QString problem;
            self->removeFile(folder, fileName, &problem);
            Q_EMIT self->changed();
            settled();
            return;
        }
        self->store(token, folder, fileName, remote.id, [settled](const QString &) { settled(); });
    };
    const auto failed = [refused, done](const GraphError &error) {
        if (later(error))
            done(error.message);
        else
            refused(error.message);
    };
    if (waiting.removed) {
        if (remote.id.isEmpty()) {
            settled();
            return;
        }
        m_graph->send(token, "DELETE", u"me/events/%1"_s.arg(encoded(remote.id)), {},
                      [self, folder, fileName, settled, failed](const QJsonObject &,
                                                                const GraphError &error) {
                          if (!self)
                              return;
                          if (error && error.status != 404) {
                              failed(error);
                              return;
                          }
                          State state = readState(folder.path);
                          state.files.remove(fileName);
                          writeState(folder.path, state);
                          QFile::remove(folder.path + u'/' + kCopies + u'/' + fileName);
                          settled();
                      });
        return;
    }
    const ics::Group after = ics::parse(readFile(folder.path + u'/' + fileName));
    const auto master = masterOf(after);
    if (!master) {
        refused(tr("Outlook keeps an occurrence only with its series."));
        return;
    }
    QString problem;
    auto fields = graph::fieldsFor(*master, &problem);
    if (!fields) {
        refused(problem);
        return;
    }
    const bool teams = state.teams.contains(fileName) && !state.meetings.isEmpty();
    if (teams) {
        fields->insert(u"isOnlineMeeting"_s, true);
        fields->insert(u"onlineMeetingProvider"_s, state.meetings);
    }
    const QString self_ = folder.self;
    // Once Outlook has the series as it should be, it comes back down.
    const auto then = [self, token, folder, fileName, settled, failed](const QString &id) {
        return [self, token, folder, fileName, id, settled, failed](const GraphError &error) {
            if (!self)
                return;
            if (error) {
                failed(error);
                return;
            }
            self->store(token, folder, fileName, id, [settled](const QString &) { settled(); });
        };
    };
    const auto occurrences = [self, token, after, self_,
                              then](const QString &id, const ics::Group &before, bool organizer) {
        self->sendOccurrences(token, id, before, after, organizer, self_, then(id));
    };
    if (remote.id.isEmpty()) {
        m_graph->send(token, "POST", u"me/calendars/%1/events"_s.arg(encoded(state.calendarId)),
                      *fields,
                      [self, fileName, folder, master, occurrences,
                       failed](const QJsonObject &json, const GraphError &error) {
                          if (!self)
                              return;
                          if (error) {
                              failed(error);
                              return;
                          }
                          const QString id = json[u"id"].toString();
                          State state = readState(folder.path);
                          state.files.insert(fileName, {id, json[u"changeKey"].toString(), true});
                          writeState(folder.path, state);
                          occurrences(id, {master}, true);
                      });
        return;
    }
    const ics::Group before = serverCopy(folder, fileName);
    const auto oldMaster = masterOf(before);
    QString ignored;
    const auto oldFields = oldMaster ? graph::fieldsFor(*oldMaster, &ignored) : std::nullopt;
    if (!remote.organizer || (oldFields == fields && !teams)) {
        occurrences(remote.id, before, remote.organizer);
        return;
    }
    m_graph->send(
        token, "PATCH", u"me/events/%1"_s.arg(encoded(remote.id)), *fields,
        [self, remote, before, occurrences, failed](const QJsonObject &, const GraphError &error) {
            if (!self)
                return;
            if (error) {
                failed(error);
                return;
            }
            occurrences(remote.id, before, true);
        });
}

void MicrosoftSource::sendOccurrences(const QString &token, const QString &id,
                                      const ics::Group &before, const ics::Group &after,
                                      bool organizer, const QString &self,
                                      std::function<void(const GraphError &)> done)
{
    QList<Step> steps = stepsBetween(before, after, organizer, self);
    // One step at a time, each finding its occurrence first.
    auto next = std::make_shared<std::function<void()>>();
    const QPointer<MicrosoftSource> source(this);
    *next = [source, token, id, steps, done, next]() mutable {
        if (!source)
            return;
        if (steps.isEmpty()) {
            done({});
            *next = nullptr;
            return;
        }
        const Step step = steps.takeFirst();
        const auto act = [source, token, step, done, next](const QString &target) {
            QByteArray method = "PATCH";
            QString path = u"me/events/%1"_s.arg(encoded(target));
            QJsonObject body = step.fields;
            if (step.kind == Step::Delete) {
                method = "DELETE";
            } else if (step.kind == Step::Answer) {
                method = "POST";
                path += u'/' + step.action;
                body = {{u"sendResponse"_s, true}};
            }
            source->m_graph->send(token, method, path, body,
                                  [done, next](const QJsonObject &, const GraphError &error) {
                                      if (error && error.status != 404) {
                                          done(error);
                                          *next = nullptr;
                                          return;
                                      }
                                      (*next)();
                                  });
        };
        if (!step.occurrence.isValid()) {
            act(id);
            return;
        }
        // A day either side finds it whatever zone it is shown in.
        const QDateTime at = step.occurrence.toUTC();
        source->m_graph->list(
            token,
            u"me/events/%1/instances?startDateTime=%2&endDateTime=%3&$select=id,originalStart"_s
                .arg(encoded(id), at.addDays(-1).toString(Qt::ISODate),
                     at.addDays(1).toString(Qt::ISODate)),
            [step, at, act, done, next](const QList<QJsonObject> &instances,
                                        const GraphError &error) {
                if (error) {
                    done(error);
                    *next = nullptr;
                    return;
                }
                const QDateTime day(step.occurrence.date(), QTime(0, 0), QTimeZone::UTC);
                for (const QJsonObject &instance : instances) {
                    const QDateTime original =
                        QDateTime::fromString(instance[u"originalStart"].toString(), Qt::ISODate);
                    if (step.allDay ? std::abs(original.secsTo(day)) <= 14 * 3600
                                    : original == at) {
                        act(instance[u"id"].toString());
                        return;
                    }
                }
                // Already gone, as a deleted occurrence may be.
                (*next)();
            });
    };
    (*next)();
}

void MicrosoftSource::readContacts(const QString &accountId, const QString &token)
{
    m_contactsRead.insert(accountId);
    const QPointer<MicrosoftSource> self(this);
    m_graph->send(token, "GET", u"me/people?$top=200&$select=displayName,scoredEmailAddresses"_s,
                  {}, [self, accountId](const QJsonObject &json, const GraphError &error) {
                      if (!self)
                          return;
                      if (error) {
                          // Tried again next run; guest suggestions can wait.
                          self->m_contactsRead.remove(accountId);
                          return;
                      }
                      QList<Contact> contacts;
                      for (const QJsonValue &person : json[u"value"].toArray()) {
                          const QString email =
                              person[u"scoredEmailAddresses"][0][u"address"].toString();
                          if (!email.isEmpty())
                              contacts << Contact{person[u"displayName"].toString(), email};
                      }
                      Q_EMIT self->contactsFound(accountId, contacts);
                  });
}

QDateTime MicrosoftSource::lastSynced() const
{
    QDateTime oldest;
    for (const QString &account : m_accounts) {
        const QDateTime synced = readAccount(accountPath(account)).synced;
        if (synced.isValid() && (!oldest.isValid() || synced < oldest))
            oldest = synced;
    }
    return oldest;
}

QString MicrosoftSource::lastError() const
{
    for (const QString &account : m_accounts) {
        if (const QString error = readAccount(accountPath(account)).error; !error.isEmpty())
            return u"%1: %2"_s.arg(account, error);
    }
    return {};
}

QVariantList MicrosoftSource::syncReport() const
{
    QVariantList report;
    for (const QString &account : m_accounts) {
        const AccountState state = readAccount(accountPath(account));
        report << QVariantMap{{u"account"_s, account},
                              {u"lastSynced"_s, state.synced.toLocalTime()},
                              {u"error"_s, state.error},
                              {u"problems"_s, QStringList()}};
    }
    return report;
}

QStringList MicrosoftSource::waitingChanges() const
{
    QStringList waiting;
    for (const Folder &folder : folders()) {
        const State state = readState(folder.path);
        QSet<QString> files(state.teams);
        for (auto it = state.pending.cbegin(); it != state.pending.cend(); ++it)
            files.insert(it.key());
        if (!files.isEmpty())
            waiting << tr("%1 changes not sent yet to %2").arg(files.size()).arg(folder.account);
    }
    return waiting;
}

MicrosoftSource::State MicrosoftSource::readState(const QString &folder)
{
    const QJsonObject json = readJson(folder + u'/' + kStateFile);
    State state;
    state.calendarId = json[u"calendarId"].toString();
    state.writable = json[u"writable"].toBool(true);
    state.meetings = json[u"meetings"].toString();
    const QJsonObject files = json[u"files"].toObject();
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QJsonObject file = it->toObject();
        state.files.insert(it.key(), {file[u"id"].toString(), file[u"changeKey"].toString(),
                                      file[u"organizer"].toBool(true)});
    }
    const QJsonObject pending = json[u"pending"].toObject();
    for (auto it = pending.begin(); it != pending.end(); ++it)
        state.pending.insert(
            it.key(), {it->toObject()[u"removed"].toBool(), it->toObject()[u"seq"].toInteger()});
    for (const QJsonValue &file : json[u"teams"].toArray())
        state.teams.insert(file.toString());
    state.seq = json[u"seq"].toInteger();
    return state;
}

void MicrosoftSource::writeState(const QString &folder, const State &state)
{
    QJsonObject files;
    for (auto it = state.files.cbegin(); it != state.files.cend(); ++it)
        files.insert(it.key(), QJsonObject{{u"id"_s, it->id},
                                           {u"changeKey"_s, it->changeKey},
                                           {u"organizer"_s, it->organizer}});
    QJsonObject pending;
    for (auto it = state.pending.cbegin(); it != state.pending.cend(); ++it)
        pending.insert(it.key(), QJsonObject{{u"removed"_s, it->removed}, {u"seq"_s, it->seq}});
    writeJson(folder + u'/' + kStateFile,
              {{u"calendarId"_s, state.calendarId},
               {u"writable"_s, state.writable},
               {u"meetings"_s, state.meetings},
               {u"files"_s, files},
               {u"pending"_s, pending},
               {u"teams"_s,
                QJsonArray::fromStringList(QStringList(state.teams.cbegin(), state.teams.cend()))},
               {u"seq"_s, state.seq}});
}

MicrosoftSource::AccountState MicrosoftSource::readAccount(const QString &path)
{
    const QJsonObject json = readJson(path + u'/' + kAccountFile);
    return {QDateTime::fromString(json[u"synced"].toString(), Qt::ISODate),
            json[u"error"].toString()};
}

void MicrosoftSource::writeAccount(const QString &path, const AccountState &state)
{
    writeJson(path + u'/' + kAccountFile,
              {{u"synced"_s, state.synced.toString(Qt::ISODate)}, {u"error"_s, state.error}});
}

ics::Group MicrosoftSource::serverCopy(const Folder &folder, const QString &fileName)
{
    return ics::parse(readFile(folder.path + u'/' + kCopies + u'/' + fileName));
}

void MicrosoftSource::keepServerCopy(const Folder &folder, const QString &fileName,
                                     const QByteArray &data)
{
    QDir().mkpath(folder.path + u'/' + kCopies);
    QSaveFile file(folder.path + u'/' + kCopies + u'/' + fileName);
    if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size())
        file.commit();
}

} // namespace callie
