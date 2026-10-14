#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QUrl>

namespace callie {

class CalendarSource;

/// Saving a calendar, or one event, as an .ics file. main.cpp sets it up; it
/// says what it saved, or why it could not, through its signals.
class ExportController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Exporter)
    QML_SINGLETON

public:
    static ExportController *instance();
    static ExportController *create(QQmlEngine *, QJSEngine *);

    void setUp(CalendarSource *calendars);

    /// A file name for `name`, such as "book-club.ics".
    Q_INVOKABLE static QString fileNameFor(const QString &name);
    /// Where a save dialog first offers to put `name`: the Documents folder.
    Q_INVOKABLE static QUrl suggestedFile(const QString &name);
    /// Saves the calendar, or with `uid` that event and its series, to `file`.
    Q_INVOKABLE bool save(const QString &calendarId, const QString &uid, const QUrl &file);

Q_SIGNALS:
    void saved(const QString &fileName);
    void failed(const QString &error);

private:
    explicit ExportController(QObject *parent);

    QPointer<CalendarSource> m_calendars;
};

} // namespace callie
