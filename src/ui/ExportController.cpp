#include "ExportController.h"

#include "callie/CalendarSource.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace callie {

ExportController::ExportController(QObject *parent) : QObject(parent) {}

ExportController *ExportController::instance()
{
    static auto *exporter = new ExportController(QCoreApplication::instance());
    return exporter;
}

ExportController *ExportController::create(QQmlEngine *, QJSEngine *)
{
    ExportController *exporter = instance();
    QJSEngine::setObjectOwnership(exporter, QJSEngine::CppOwnership);
    return exporter;
}

void ExportController::setUp(CalendarSource *calendars)
{
    m_calendars = calendars;
}

QString ExportController::fileNameFor(const QString &name)
{
    QString base = name.toLower();
    // Letters in any script stay; the rest become dashes.
    base.replace(QRegularExpression(u"[^\\w]+"_s, QRegularExpression::UseUnicodePropertiesOption),
                 u"-"_s);
    base.remove(QRegularExpression(u"^-+|-+$"_s));
    return (base.isEmpty() ? u"calendar"_s : base) + u".ics"_s;
}

QUrl ExportController::suggestedFile(const QString &name)
{
    return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
                               u'/' + fileNameFor(name));
}

bool ExportController::save(const QString &calendarId, const QString &uid, const QUrl &file)
{
    QString error;
    const QByteArray data =
        m_calendars ? m_calendars->exportEvents(calendarId, uid, &error) : QByteArray();
    if (data.isEmpty()) {
        Q_EMIT failed(error.isEmpty() ? tr("There is nothing to save.") : error);
        return false;
    }
    const QString path = file.toLocalFile();
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) {
        Q_EMIT failed(tr("Callie could not save %1: %2").arg(path, out.errorString()));
        return false;
    }
    Q_EMIT saved(QFileInfo(path).fileName());
    return true;
}

} // namespace callie
