#include "ImportController.h"

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

void ImportController::setUp(LocalCalendars *local)
{
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
    if (!m_local)
        return list;
    for (const CalendarInfo &calendar : m_local->calendars()) {
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
    const ics::Group events = ics::parse(file.readAll(), &problem);
    if (events.isEmpty()) {
        setError(problem.isEmpty() ? tr("%1 has no events in it.").arg(QFileInfo(path).fileName())
                                   : tr("%1 is %2.").arg(QFileInfo(path).fileName(), problem));
        return false;
    }
    m_fileName = QFileInfo(path).fileName();
    m_events = events;
    setError({});
    Q_EMIT pendingChanged();
    return true;
}

bool ImportController::add(const QString &calendarId)
{
    if (m_events.isEmpty())
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
    for (const CalendarInfo &calendar : m_local ? m_local->calendars() : QList<CalendarInfo>()) {
        if (calendar.id != id)
            continue;
        const int written = m_local->importEvents(id, m_events, &error);
        if (written < 0) {
            setError(error);
            return false;
        }
        m_events.clear();
        m_fileName.clear();
        setError({});
        Q_EMIT pendingChanged();
        Q_EMIT added(written, calendar.displayName);
        return true;
    }
    setError(tr("That calendar is not on this computer any more."));
    return false;
}

void ImportController::cancel()
{
    m_events.clear();
    m_fileName.clear();
    setError({});
    Q_EMIT pendingChanged();
}

void ImportController::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    Q_EMIT errorChanged();
}

} // namespace callie
