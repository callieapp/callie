#include "ImportController.h"

#include "callie/CalendarSource.h"
#include "callie/LocalCalendars.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

constexpr int kPreviewed = 5;
constexpr qint64 kLargestFile = 32 * 1024 * 1024;

} // namespace

ImportController::ImportController(QObject *parent) : QObject(parent) {}

ImportController *ImportController::instance()
{
    static auto *importer = new ImportController(QCoreApplication::instance());
    return importer;
}

ImportController *ImportController::create(QQmlEngine *, QJSEngine *)
{
    ImportController *importer = instance();
    QJSEngine::setObjectOwnership(importer, QJSEngine::CppOwnership);
    return importer;
}

void ImportController::setUp(CalendarSource *calendars, LocalCalendars *local)
{
    m_calendars = calendars;
    m_local = local;
    Q_EMIT pendingChanged();
}

int ImportController::count() const
{
    return int(ics::byUid(m_events).size());
}

QVariantList ImportController::preview() const
{
    QList<KCalendarCore::Event::Ptr> firsts;
    for (const ics::Group &group : ics::byUid(m_events))
        firsts.append(group.first());
    std::ranges::sort(firsts, {}, [](const auto &event) { return event->dtStart(); });
    QVariantList list;
    for (const auto &event : firsts.first(std::min<qsizetype>(kPreviewed, firsts.size())))
        list << QVariantMap{{u"summary"_s, event->summary()},
                            {u"start"_s, event->allDay()
                                             ? QDateTime(event->dtStart().date(), QTime(0, 0))
                                             : event->dtStart()},
                            {u"allDay"_s, event->allDay()}};
    return list;
}

QVariantList ImportController::calendars() const
{
    QVariantList list;
    for (const CalendarInfo &calendar :
         m_calendars ? m_calendars->calendars() : QList<CalendarInfo>()) {
        if (calendar.writable)
            list << QVariantMap{{u"id"_s, calendar.id}, {u"name"_s, calendar.displayName}};
    }
    if (m_local)
        list << QVariantMap{
            {u"id"_s, u"new"_s},
            {u"name"_s, tr("A new calendar, %1").arg(QFileInfo(m_fileName).completeBaseName())}};
    return list;
}

bool ImportController::accepts(const QUrl &url)
{
    return url.isLocalFile() && url.toLocalFile().endsWith(u".ics"_s, Qt::CaseInsensitive);
}

bool ImportController::open(const QList<QUrl> &urls)
{
    const auto found = std::ranges::find_if(urls, &ImportController::accepts);
    // Nothing to open is no error, and leaves none from before.
    if (found == urls.end()) {
        setError({});
        return false;
    }
    const QString path = found->toLocalFile();
    QFile file(path);
    if (file.size() > kLargestFile) {
        setError(tr("%1 is too large to be a calendar.").arg(QFileInfo(path).fileName()));
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        setError(tr("Callie could not read %1: %2").arg(path, file.errorString()));
        return false;
    }
    QString problem;
    const QByteArray data = file.readAll();
    const ics::Group events = ics::parse(data, &problem);
    if (events.isEmpty()) {
        setError(problem.isEmpty() ? tr("%1 has no events in it.").arg(QFileInfo(path).fileName())
                                   : tr("%1 is %2.").arg(QFileInfo(path).fileName(), problem));
        return false;
    }
    m_fileName = QFileInfo(path).fileName();
    m_data = data;
    m_events = events;
    ++m_file;
    setError({});
    Q_EMIT pendingChanged();
    return true;
}

bool ImportController::add(const QString &calendarId)
{
    if (m_events.isEmpty() || m_busy)
        return false;
    QString error;
    QString id = calendarId;
    if (id == u"new") {
        if (!m_local) {
            setError(tr("Calendars on this computer cannot be changed here."));
            return false;
        }
        id = m_local->create(QFileInfo(m_fileName).completeBaseName(), {}, &error);
        if (id.isEmpty()) {
            setError(error);
            return false;
        }
    }
    const QList<CalendarInfo> calendars =
        m_calendars ? m_calendars->calendars() : QList<CalendarInfo>();
    const auto calendar = std::ranges::find(calendars, id, &CalendarInfo::id);
    if (calendar == calendars.cend()) {
        setError(tr("That calendar is not connected any more."));
        return false;
    }
    setError({});
    setBusy(true);
    const QPointer<ImportController> self(this);
    m_calendars->importEvents(
        id, m_data,
        [self, name = calendar->displayName, file = m_file](int count, const QString &failed) {
            if (!self)
                return;
            self->setBusy(false);
            // Some may have gone in before the failure; sending again replaces them.
            if (!failed.isEmpty()) {
                if (file == self->m_file)
                    self->setError(count > 0
                                       ? tr("Only %n events went in: %1", "", count).arg(failed)
                                       : failed);
                return;
            }
            if (file == self->m_file && !self->m_events.isEmpty()) {
                self->m_events.clear();
                self->m_data.clear();
                self->m_fileName.clear();
                Q_EMIT self->pendingChanged();
            }
            Q_EMIT self->added(count, name);
        });
    return true;
}

void ImportController::cancel()
{
    ++m_file;
    m_events.clear();
    m_data.clear();
    m_fileName.clear();
    setError({});
    Q_EMIT pendingChanged();
}

void ImportController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    Q_EMIT busyChanged();
}

void ImportController::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    Q_EMIT errorChanged();
}

} // namespace callie
