#pragma once

#include "callie/Ics.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantList>

namespace callie {

class LocalCalendars;
class VdirSource;

/// Adding the events of an .ics file to a calendar: open() reads the file and
/// shows what is in it, and add() writes it into the calendar picked. Files
/// come from the command line, the file manager or a drop on the window.
class ImportController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Importer)
    QML_SINGLETON
    /// A file is read and waiting for a calendar to go into.
    Q_PROPERTY(bool pending READ pending NOTIFY pendingChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY pendingChanged)
    Q_PROPERTY(int count READ count NOTIFY pendingChanged)
    /// {summary, start, allDay} of the first few events, soonest first.
    Q_PROPERTY(QVariantList preview READ preview NOTIFY pendingChanged)
    /// {id, name} of each calendar the events can go into; "new" makes one
    /// named after the file.
    Q_PROPERTY(QVariantList calendars READ calendars NOTIFY pendingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    static ImportController *instance();
    static ImportController *create(QQmlEngine *, QJSEngine *);

    /// Where events can go: `local` can also take a calendar made for them.
    void setUp(LocalCalendars *local, const QList<VdirSource *> &others = {});

    [[nodiscard]] bool pending() const { return !m_events.isEmpty(); }
    [[nodiscard]] QString fileName() const { return m_fileName; }
    [[nodiscard]] int count() const;
    [[nodiscard]] QVariantList preview() const;
    [[nodiscard]] QVariantList calendars() const;
    [[nodiscard]] QString error() const { return m_error; }

    /// Whether `url` is something open() takes.
    Q_INVOKABLE static bool accepts(const QUrl &url);
    /// Reads the first .ics file among `urls`. False when there was none, or
    /// it could not be read, with error set.
    Q_INVOKABLE bool open(const QList<QUrl> &urls);
    /// Writes the waiting events into `calendarId`, or a new calendar for "new".
    Q_INVOKABLE bool add(const QString &calendarId);
    Q_INVOKABLE void cancel();

Q_SIGNALS:
    void pendingChanged();
    void errorChanged();
    /// The events went in; for a note saying so.
    void added(int count, const QString &calendarName);

private:
    explicit ImportController(QObject *parent);
    void setError(const QString &error);
    [[nodiscard]] QList<VdirSource *> sources() const;

    QPointer<LocalCalendars> m_local;
    QList<QPointer<VdirSource>> m_others;
    QString m_fileName;
    ics::Group m_events;
    QString m_error;
};

} // namespace callie
