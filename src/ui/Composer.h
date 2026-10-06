#pragma once

#include "callie/CalendarSource.h"
#include "callie/QuickAdd.h"

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QVariantList>

namespace callie {

/// Turns what the user types into a new event: reads the line as they type,
/// offers the calendars that take new events, and creates it.
class Composer : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(callie::CalendarSource *source READ source WRITE setSource NOTIFY sourceChanged)
    /// The one-line description, read by QuickAdd.
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY draftChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY draftChanged)
    Q_PROPERTY(QString location READ location NOTIFY draftChanged)
    Q_PROPERTY(QDateTime start READ start NOTIFY draftChanged)
    Q_PROPERTY(QDateTime end READ end NOTIFY draftChanged)
    Q_PROPERTY(bool allDay READ allDay NOTIFY draftChanged)
    /// {id, name, color} for each shown calendar that takes new events.
    Q_PROPERTY(QVariantList calendars READ calendars NOTIFY calendarsChanged)
    /// Where the event goes; starts as the one used last.
    Q_PROPERTY(QString calendarId READ calendarId WRITE setCalendarId NOTIFY calendarIdChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY draftChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit Composer(QObject *parent = nullptr);

    [[nodiscard]] CalendarSource *source() const { return m_source; }
    void setSource(CalendarSource *source);
    [[nodiscard]] QString text() const { return m_text; }
    void setText(const QString &text);

    [[nodiscard]] QString summary() const { return m_draft.summary; }
    [[nodiscard]] QString location() const { return m_draft.location; }
    [[nodiscard]] QDateTime start() const { return m_draft.start; }
    [[nodiscard]] QDateTime end() const { return m_draft.end; }
    [[nodiscard]] bool allDay() const { return m_draft.allDay; }
    [[nodiscard]] QVariantList calendars() const { return m_calendars; }
    [[nodiscard]] QString calendarId() const { return m_calendarId; }
    void setCalendarId(const QString &id);
    [[nodiscard]] bool ready() const;
    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] QString error() const { return m_error; }

    /// Reads the text again against the current time, for a reopened composer.
    Q_INVOKABLE void reset();
    /// Starts from a time picked on the grid, which the text's own day or time
    /// replaces if it names one. An invalid start clears it.
    Q_INVOKABLE void pickTimes(const QDateTime &start, const QDateTime &end);
    Q_INVOKABLE void submit();

Q_SIGNALS:
    void sourceChanged();
    void draftChanged();
    void calendarsChanged();
    void calendarIdChanged();
    void busyChanged();
    void errorChanged();
    /// The event exists now.
    void created();

private:
    void reparse();
    void refreshCalendars();
    void setError(const QString &error);

    QPointer<CalendarSource> m_source;
    QString m_text;
    EventDraft m_draft;
    QDateTime m_pickedStart;
    QDateTime m_pickedEnd;
    QVariantList m_calendars;
    QString m_calendarId;
    bool m_busy = false;
    QString m_error;
};

} // namespace callie
