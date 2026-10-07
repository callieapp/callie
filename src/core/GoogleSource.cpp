#include "callie/GoogleSource.h"

#include "callie/GoogleCache.h"
#include "callie/GoogleRecurrence.h"
#include "callie/GoogleSync.h"
#include "callie/Logging.h"
#include "callie/Times.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QPromise>
#include <QUuid>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

QString calendarKey(const Account &account, const QString &calendarId)
{
    return account.id + u'/' + calendarId;
}

bool canWrite(const QString &accessRole)
{
    return accessRole == QLatin1String("owner") || accessRole == QLatin1String("writer");
}

QList<CalendarInfo> readCalendars(GoogleCache &cache, const QList<Account> &accounts)
{
    QList<CalendarInfo> result;
    for (const Account &account : accounts) {
        for (const GoogleCalendar &calendar : cache.calendars(account)) {
            result.append(CalendarInfo{
                .id = calendarKey(account, calendar.id),
                .displayName = calendar.summary,
                .color = QColor::fromString(calendar.color),
                .writable = canWrite(calendar.accessRole),
                // Google's own "show in list" choice, until Callie has its own.
                .enabled = calendar.selected,
                .account = account.id,
            });
        }
    }
    return result;
}

QList<Event> readEvents(GoogleCache &cache, const QList<Account> &accounts, const QDateTime &from,
                        const QDateTime &to, const QTimeZone &tz)
{
    QList<Event> result;
    for (const Account &account : accounts) {
        for (const GoogleCalendar &calendar : cache.calendars(account)) {
            if (!calendar.selected)
                continue;
            const QColor color = QColor::fromString(calendar.color);
            QList<GoogleEvent> stored = cache.events(account, calendar.id, from, to);
            for (GoogleEvent &event : stored) {
                if (event.remindersUseDefault)
                    event.reminders = calendar.defaultReminders;
            }
            const QList<Event> events = expandGoogleEvents(stored, from, to, tz);
            const bool writable = canWrite(calendar.accessRole);
            for (Event event : events) {
                event.calendarId = calendarKey(account, calendar.id);
                event.canEdit = event.canEdit && writable;
                event.canRespond = event.canRespond && writable;
                event.color = color;
                event.start = event.start.toTimeZone(tz);
                event.end = event.end.toTimeZone(tz);
                result.append(event);
            }
        }
    }
    return result;
}

/// What Google is sent to make `edit` on `event`, or on its whole series with
/// `series`. Empty when a series' times are needed and not stored yet.
struct EditFields
{
    QJsonObject fields;
    /// The fields add or remove a video call.
    bool conference = false;
};

std::optional<EditFields> editFields(const Event &event, const EventEdit &edit,
                                     const std::optional<GoogleEvent> &stored, bool series)
{
    EditFields result;
    QJsonObject &fields = result.fields;
    if (edit.summary)
        fields.insert(u"summary"_s, *edit.summary);
    if (edit.location)
        fields.insert(u"location"_s, *edit.location);
    if (edit.description)
        fields.insert(u"description"_s, *edit.description);

    if (edit.movesTimes()) {
        const bool allDay = edit.allDay.value_or(event.allDay);
        // Times the edit leaves alone stay on the event's own clock, not the viewer's.
        QTimeZone own(event.zone.toUtf8());
        if (!own.isValid() && stored && !stored->start.timeZone.isEmpty())
            own = QTimeZone(stored->start.timeZone.toUtf8());
        if (!own.isValid())
            own = event.start.timeZone();
        const QDateTime start = edit.start.value_or(event.start.toTimeZone(own));
        const QDateTime end = edit.end.value_or(event.end.toTimeZone(own));
        // A series keeps its first day: it moves by as many days as this
        // occurrence did, to the time of day this occurrence now has.
        QDate firstDay = start.date();
        QDateTime newStart = start;
        if (series) {
            if (!stored || !(stored->start.dateTime.isValid() || stored->start.date.isValid())) {
                return std::nullopt;
            }
            const QTimeZone was = stored->start.timeZone.isEmpty()
                                      ? event.start.timeZone()
                                      : QTimeZone(stored->start.timeZone.toUtf8());
            const qint64 days = event.start.toTimeZone(was).date().daysTo(start.date());
            const QDate seriesDay = stored->start.isAllDay()
                                        ? stored->start.date
                                        : stored->start.dateTime.toTimeZone(was).date();
            firstDay = seriesDay.addDays(days);
            newStart = QDateTime(firstDay, start.time(), start.timeZone());
        }
        if (allDay) {
            const qint64 length = std::max<qint64>(1, start.date().daysTo(end.date()));
            // Google merges nested objects, so a timed event's time is cleared.
            const auto day = [](QDate date) {
                return QJsonObject{{u"date"_s, date.toString(Qt::ISODate)},
                                   {u"dateTime"_s, QJsonValue::Null},
                                   {u"timeZone"_s, QJsonValue::Null}};
            };
            fields.insert(u"start"_s, day(firstDay));
            fields.insert(u"end"_s, day(firstDay.addDays(length)));
        } else {
            const QDateTime newEnd = newStart.addSecs(start.secsTo(end));
            // And an all-day event's date, which would otherwise stay.
            const auto time = [](const QDateTime &moment) {
                QJsonObject json{{u"dateTime"_s, moment.toString(Qt::ISODate)},
                                 {u"date"_s, QJsonValue::Null}};
                if (moment.timeSpec() == Qt::TimeZone)
                    json.insert(u"timeZone"_s, QString::fromUtf8(moment.timeZone().id()));
                return json;
            };
            fields.insert(u"start"_s, time(newStart));
            fields.insert(u"end"_s, time(newEnd));
        }
    }

    if (edit.recurrence)
        fields.insert(u"recurrence"_s, QJsonArray::fromStringList(*edit.recurrence));

    if (edit.guests) {
        // Guests who stay keep their answers; the user stays on the list.
        const QJsonArray before =
            QJsonDocument::fromJson(stored ? stored->attendees : QByteArray()).array();
        QJsonArray after;
        for (const QJsonValue &guest : before) {
            if (guest[u"self"].toBool() || guest[u"resource"].toBool() ||
                edit.guests->contains(guest[u"email"].toString(), Qt::CaseInsensitive))
                after.append(guest);
        }
        for (const QString &email : *edit.guests) {
            const bool known =
                std::any_of(after.cbegin(), after.cend(), [&email](const QJsonValue &g) {
                    return g[u"email"].toString().compare(email, Qt::CaseInsensitive) == 0;
                });
            if (!known)
                after.append(QJsonObject{{u"email"_s, email}});
        }
        fields.insert(u"attendees"_s, after);
    }

    if (edit.videoCall) {
        result.conference = true;
        if (*edit.videoCall) {
            fields.insert(
                u"conferenceData"_s,
                QJsonObject{{u"createRequest"_s,
                             QJsonObject{{u"requestId"_s,
                                          QUuid::createUuid().toString(QUuid::WithoutBraces)},
                                         {u"conferenceSolutionKey"_s,
                                          QJsonObject{{u"type"_s, u"hangoutsMeet"_s}}}}}});
        } else {
            fields.insert(u"conferenceData"_s, QJsonValue::Null);
        }
    }

    return result;
}

} // namespace

GoogleSource::GoogleSource(GoogleCache &cache, QList<Account> accounts, QObject *parent)
    : CalendarSource(parent), m_cache(cache), m_accounts(std::move(accounts))
{
    m_readers.setMaxThreadCount(1);
    m_changes.setSingleShot(true);
    m_changes.setInterval(250);
    connect(&m_changes, &QTimer::timeout, this, &CalendarSource::changed);

    loadStatus();
}

void GoogleSource::loadStatus()
{
    m_lastSynced = {};
    m_lastError.clear();
    // From what the last run recorded, so the title bar is right before the
    // first sync of this run finishes.
    for (const Account &account : std::as_const(m_accounts)) {
        const SyncState state = m_cache.accountState(account);
        if (state.lastSynced > m_lastSynced)
            m_lastSynced = state.lastSynced;
        QStringList errors;
        if (!state.lastError.isEmpty())
            errors.append(state.lastError);
        // A run can fail per calendar while the account itself is fine.
        for (const GoogleCalendar &calendar : m_cache.calendars(account)) {
            const QString error = m_cache.calendarState(account, calendar.id).lastError;
            if (!error.isEmpty())
                errors.append(QStringLiteral("%1: %2").arg(calendar.summary, error));
        }
        for (const QString &error : std::as_const(errors))
            m_lastError += (m_lastError.isEmpty() ? QString() : QStringLiteral("\n")) +
                           QStringLiteral("%1: %2").arg(account.id, error);
    }
}

void GoogleSource::setSync(GoogleSync *sync)
{
    if (m_sync)
        disconnect(m_sync, nullptr, this, nullptr);
    m_sync = sync;
    if (m_sync)
        connect(m_sync, &GoogleSync::changed, this, [this] {
            if (!m_changes.isActive())
                m_changes.start();
        });
}

void GoogleSource::setAccounts(QList<Account> accounts)
{
    if (m_accounts == accounts)
        return;
    if (m_sync) {
        for (const Account &account : std::as_const(m_accounts)) {
            if (!accounts.contains(account))
                m_sync->forget(account);
        }
    }
    m_accounts = std::move(accounts);
    // A removed account's errors and sync time go with it; a new one starts unsynced.
    loadStatus();
    Q_EMIT statusChanged();
    Q_EMIT changed();
    if (m_sync && !m_accounts.isEmpty())
        refresh();
}

QList<CalendarInfo> GoogleSource::calendars() const
{
    return readCalendars(m_cache, m_accounts);
}

QList<Event> GoogleSource::eventsBetween(const QDateTime &from, const QDateTime &to,
                                         const QTimeZone &tz) const
{
    return readEvents(m_cache, m_accounts, from, to, tz);
}

QFuture<SourceSnapshot> GoogleSource::load(const QDateTime &from, const QDateTime &to,
                                           const QTimeZone &tz) const
{
    auto promise = std::make_shared<QPromise<SourceSnapshot>>();
    QFuture<SourceSnapshot> future = promise->future();
    promise->start();
    m_readers.start([path = m_cache.path(), accounts = m_accounts, from, to, tz, promise] {
        GoogleCache reader(path);
        SourceSnapshot snapshot;
        if (reader.openForReading()) {
            snapshot.calendars = readCalendars(reader, accounts);
            snapshot.events = readEvents(reader, accounts, from, to, tz);
        } else {
            qCWarning(lcSync) << "could not read the cache:" << reader.errorString();
        }
        promise->addResult(snapshot);
        promise->finish();
    });
    return future;
}

std::optional<std::pair<Account, QString>> GoogleSource::splitCalendarId(const QString &id) const
{
    for (const Account &account : m_accounts) {
        const QString prefix = calendarKey(account, {});
        if (id.startsWith(prefix))
            return std::pair(account, id.mid(prefix.size()));
    }
    return std::nullopt;
}

GoogleSync::Target GoogleSource::target(const QString &calendarId, const Event &event,
                                        bool wholeSeries)
{
    GoogleSync::Target target;
    target.calendarId = calendarId;
    target.seriesId = event.seriesId;
    target.eventId = wholeSeries && !event.seriesId.isEmpty() ? event.seriesId : event.eventId;
    if (event.allDay)
        target.originalStart.date = event.recurrenceId.date();
    else
        target.originalStart.dateTime = event.recurrenceId;
    return target;
}

void GoogleSource::createEvent(const EventDraft &draft, Created done)
{
    const auto calendar = splitCalendarId(draft.calendarId);
    if (!m_sync) {
        done({tr("Callie is not connected to Google right now."), true});
        return;
    }
    if (!calendar) {
        done(tr("That calendar is not in any connected account."));
        return;
    }
    m_sync->createEvent(calendar->first, calendar->second, draft, std::move(done));
}

void GoogleSource::respond(const Event &event, const QString &status, bool wholeSeries,
                           Created done)
{
    const auto calendar = splitCalendarId(event.calendarId);
    // Not syncing yet can pass; an account that is gone will not come back.
    if (!m_sync) {
        done({tr("Callie is not connected to Google right now."), true});
        return;
    }
    if (!calendar) {
        done(tr("That event's account is no longer connected."));
        return;
    }
    m_sync->respond(calendar->first, target(calendar->second, event, wholeSeries), status,
                    std::move(done));
}

void GoogleSource::deleteEvent(const Event &event, bool wholeSeries, Created done)
{
    const auto calendar = splitCalendarId(event.calendarId);
    // Not syncing yet can pass; an account that is gone will not come back.
    if (!m_sync) {
        done({tr("Callie is not connected to Google right now."), true});
        return;
    }
    if (!calendar) {
        done(tr("That event's account is no longer connected."));
        return;
    }
    m_sync->remove(calendar->first, target(calendar->second, event, wholeSeries), std::move(done));
}

void GoogleSource::moveEvent(const Event &event, const QDateTime &start, const QDateTime &end,
                             bool wholeSeries, Created done)
{
    const auto calendar = splitCalendarId(event.calendarId);
    // Not syncing yet can pass; an account that is gone will not come back.
    if (!m_sync) {
        done({tr("Callie is not connected to Google right now."), true});
        return;
    }
    if (!calendar) {
        done(tr("That event's account is no longer connected."));
        return;
    }
    if (event.allDay) {
        done(tr("All-day events cannot be moved to a time yet."));
        return;
    }
    const auto &[account, calendarId] = *calendar;
    const bool series = wholeSeries && !event.seriesId.isEmpty();
    // An occurrence never changed before is stored only as its series, whose
    // zone it shares.
    std::optional<GoogleEvent> stored =
        m_cache.event(account, calendarId, series ? event.seriesId : event.eventId);
    if (!stored && !event.seriesId.isEmpty())
        stored = m_cache.event(account, calendarId, event.seriesId);
    const QTimeZone zone = stored && !stored->start.timeZone.isEmpty()
                               ? QTimeZone(stored->start.timeZone.toUtf8())
                               : start.timeZone();
    QDateTime newStart = start;
    QDateTime newEnd = end;
    if (series) {
        if (!stored || !stored->start.dateTime.isValid()) {
            done(tr("Callie does not have this series' times yet; try again after a sync."));
            return;
        }
        // Every occurrence moves by the shift dragged on this one, read off the
        // series' wall clock so a drag across a daylight saving change does not
        // add or lose an hour.
        const auto shift = [&zone](const QDateTime &series, const QDateTime &was,
                                   const QDateTime &now) {
            const QDateTime from = was.toTimeZone(zone);
            const QDateTime to = now.toTimeZone(zone);
            return Times::shiftWallClock(series, zone, int(from.date().daysTo(to.date())),
                                         from.time().secsTo(to.time()));
        };
        newStart = shift(stored->start.dateTime, event.start, start);
        newEnd = shift(stored->end.dateTime, event.end, end);
    }
    m_sync->move(account, calendarId, series ? event.seriesId : event.eventId,
                 newStart.toTimeZone(zone), newEnd.toTimeZone(zone), std::move(done));
}

void GoogleSource::updateEvent(const Event &event, const EventEdit &edit, EditScope scope,
                               Created done)
{
    const auto calendar = splitCalendarId(event.calendarId);
    // Not syncing yet can pass; an account that is gone will not come back.
    if (!m_sync) {
        done({tr("Callie is not connected to Google right now."), true});
        return;
    }
    if (!calendar) {
        done(tr("That event's account is no longer connected."));
        return;
    }
    const auto &[account, calendarId] = *calendar;
    if (scope == EditScope::ThisAndFollowing && !event.seriesId.isEmpty()) {
        splitSeries(account, calendarId, event, edit, std::move(done));
        return;
    }
    const bool series = scope == EditScope::AllEvents && !event.seriesId.isEmpty();
    const QString target = series ? event.seriesId : event.eventId;
    // An occurrence never changed before is stored only as its series.
    std::optional<GoogleEvent> stored = m_cache.event(account, calendarId, target);
    if (!stored && !event.seriesId.isEmpty())
        stored = m_cache.event(account, calendarId, event.seriesId);
    const std::optional<EditFields> change = editFields(event, edit, stored, series);
    if (!change) {
        done(tr("Callie does not have this series' times yet; try again after a sync."));
        return;
    }
    if (change->fields.isEmpty()) {
        done({});
        return;
    }
    m_sync->update(account, calendarId, target, change->fields, change->conference,
                   std::move(done));
}

void GoogleSource::splitSeries(const Account &account, const QString &calendarId,
                               const Event &event, const EventEdit &edit, Created done)
{
    const std::optional<GoogleEvent> stored = m_cache.event(account, calendarId, event.seriesId);
    if (!stored || stored->recurrence.isEmpty()) {
        done(tr("Callie does not have this series yet; try again after a sync."));
        return;
    }
    const QDateTime at = event.recurrenceId.isValid() ? event.recurrenceId : event.start;
    // From the first occurrence on is the whole series.
    if (stored->start.isAllDay() ? stored->start.date == at.date() : stored->start.dateTime == at) {
        updateEvent(event, edit, EditScope::AllEvents, std::move(done));
        return;
    }

    // The new series is the occurrence as edited, repeating as the series did.
    EventEdit following = edit;
    following.allDay = edit.allDay.value_or(event.allDay);
    const std::optional<EditFields> change = editFields(event, following, stored, false);
    if (!change) {
        done(tr("Callie does not have this series' times yet; try again after a sync."));
        return;
    }
    const SplitRecurrence split = splitRecurrence(*stored, at);
    QJsonObject created;
    // Named after where it splits, so a second try finds the first one's.
    created.insert(
        u"id"_s,
        QString::fromLatin1(QCryptographicHash::hash(
                                (event.seriesId + u'|' + at.toUTC().toString(Qt::ISODate)).toUtf8(),
                                QCryptographicHash::Md5)
                                .toHex()));
    created.insert(u"summary"_s, stored->summary);
    if (!stored->description.isEmpty())
        created.insert(u"description"_s, stored->description);
    if (!stored->location.isEmpty())
        created.insert(u"location"_s, stored->location);
    const QJsonArray guests = QJsonDocument::fromJson(stored->attendees).array();
    if (!guests.isEmpty())
        created.insert(u"attendees"_s, guests);
    if (!stored->remindersUseDefault) {
        QJsonArray overrides;
        for (int minutes : stored->reminders)
            overrides.append(QJsonObject{{u"method"_s, u"popup"_s}, {u"minutes"_s, minutes}});
        created.insert(u"reminders"_s,
                       QJsonObject{{u"useDefault"_s, false}, {u"overrides"_s, overrides}});
    }
    created.insert(u"recurrence"_s, QJsonArray::fromStringList(split.after));
    bool conference = change->conference;
    // A Meet call carries over, so guests keep the link they have.
    const QUrl call = stored->conferenceUrl;
    if (!edit.videoCall && call.host() == u"meet.google.com") {
        conference = true;
        created.insert(
            u"conferenceData"_s,
            QJsonObject{{u"conferenceId"_s, call.path().mid(1)},
                        {u"conferenceSolution"_s,
                         QJsonObject{{u"key"_s, QJsonObject{{u"type"_s, u"hangoutsMeet"_s}}}}},
                        {u"entryPoints"_s, QJsonArray{QJsonObject{{u"entryPointType"_s, u"video"_s},
                                                                  {u"uri"_s, call.toString()}}}}});
    }
    // The edit's fields win; the nulls that clear a patched field mean nothing here.
    for (auto it = change->fields.constBegin(); it != change->fields.constEnd(); ++it) {
        QJsonValue value = it.value();
        if (value.isNull())
            continue;
        if (value.isObject()) {
            QJsonObject object = value.toObject();
            for (const QString &key : object.keys()) {
                if (object.value(key).isNull())
                    object.remove(key);
            }
            value = object;
        }
        created.insert(it.key(), value);
    }

    const QPointer<GoogleSource> self(this);
    m_sync->insert(account, calendarId, created, conference,
                   [this, self, account, calendarId, seriesId = event.seriesId,
                    before = split.before, done](const Outcome &outcome) {
                       if (!self)
                           return;
                       if (!outcome.error.isEmpty() || !m_sync) {
                           done(outcome);
                           return;
                       }
                       m_sync->update(
                           account, calendarId, seriesId,
                           QJsonObject{{u"recurrence"_s, QJsonArray::fromStringList(before)}},
                           false, done);
                   });
}

QVariantList GoogleSource::syncReport() const
{
    QVariantList report;
    for (const Account &account : m_accounts) {
        const SyncState state = m_cache.accountState(account);
        QStringList problems;
        for (const GoogleCalendar &calendar : m_cache.calendars(account)) {
            const QString error = m_cache.calendarState(account, calendar.id).lastError;
            if (!error.isEmpty())
                problems << QStringLiteral("%1: %2").arg(calendar.summary, error);
        }
        report << QVariantMap{{QStringLiteral("account"), account.id},
                              {QStringLiteral("lastSynced"), state.lastSynced},
                              {QStringLiteral("error"), state.lastError},
                              {QStringLiteral("problems"), problems}};
    }
    return report;
}

void GoogleSource::flushChanges()
{
    if (!m_changes.isActive())
        return;
    m_changes.stop();
    Q_EMIT changed();
}

void GoogleSource::refresh()
{
    if (!m_sync) {
        Q_EMIT changed();
        return;
    }
    // GoogleSync joins a sync already running, so overlapping refreshes are cheap.
    if (m_pending == 0)
        m_runErrors.clear();
    m_pending += int(m_accounts.size());
    Q_EMIT statusChanged();
    const QPointer<GoogleSource> self(this);
    for (const Account &account : std::as_const(m_accounts)) {
        m_sync->sync(account, [this, self, account](const QStringList &errors) {
            if (!self)
                return;
            // Overlapping refreshes share one run, so each reports the same errors.
            for (const QString &error : errors) {
                const QString message = QStringLiteral("%1: %2").arg(account.id, error);
                if (m_runErrors.contains(message))
                    continue;
                m_runErrors.append(message);
                Q_EMIT errorOccurred(message);
            }
            if (--m_pending > 0)
                return;
            flushChanges();
            m_lastError = m_runErrors.join(u'\n');
            // A run for accounts removed meanwhile synced nothing still listed.
            if (m_runErrors.isEmpty() && !m_accounts.isEmpty())
                m_lastSynced = QDateTime::currentDateTimeUtc();
            Q_EMIT statusChanged();
        });
    }
}

} // namespace callie
