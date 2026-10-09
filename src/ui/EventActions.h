#pragma once

#include "callie/CalendarSource.h"

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantMap>

namespace callie {

/// What can be done to an event: answer the invitation, delete it, move it,
/// or write to its guests. Events come as EventModel's row maps.
class EventActions : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(callie::CalendarSource *source MEMBER m_source NOTIFY sourceChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    /// The event the error is about, so a card showing another event ignores it.
    Q_PROPERTY(QString errorEventId READ errorEventId NOTIFY errorChanged)

public:
    using QObject::QObject;

    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] QString errorEventId() const { return m_errorEventId; }

    /// "accepted", "tentative" or "declined".
    Q_INVOKABLE void respond(const QVariantMap &event, const QString &status, bool wholeSeries);
    Q_INVOKABLE void remove(const QVariantMap &event, bool wholeSeries);
    /// Changes the event. `changes` holds only what changes, by EventEdit's
    /// names (summary, location, description, start, end, allDay, recurrence,
    /// guests, videoCall) plus zone, the IANA zone the times are written in.
    /// `scope` is "this", "following" or "all".
    Q_INVOKABLE void update(const QVariantMap &event, const QVariantMap &changes,
                            const QString &scope);
    /// The edit `changes` describes, as update() reads it.
    [[nodiscard]] static EventEdit toEdit(const QVariantMap &changes);

    // For the editor, which works in the event's own zone, empty for the system's.
    /// The moment `minutes` past midnight on `day` on the zone's clock.
    Q_INVOKABLE static QDateTime at(const QDateTime &day, int minutes, const QString &zone);
    /// The date of `time` in the zone, as a local midnight QML keeps whole.
    Q_INVOKABLE static QDateTime dayOf(const QDateTime &time, const QString &zone);
    /// Minutes past midnight of `time` on the zone's clock.
    Q_INVOKABLE static int minutesOf(const QDateTime &time, const QString &zone);
    /// Whole days from one local midnight to another, and a midnight that many on.
    Q_INVOKABLE static int daysBetween(const QDateTime &from, const QDateTime &to);
    Q_INVOKABLE static QDateTime addDays(const QDateTime &day, int days);
    /// The repeat choices for an event starting on `day`, as {id, label}.
    Q_INVOKABLE static QVariantList repeatChoices(const QDateTime &day);
    Q_INVOKABLE static QString repeatChoice(const QStringList &recurrence, const QDateTime &day);
    Q_INVOKABLE static QStringList repeatRule(const QString &choice, const QDateTime &day);
    /// `recurrence` as the custom form edits it: {frequency, interval,
    /// weekdays, onWeekday, until, count}, or empty when the form cannot say it.
    Q_INVOKABLE static QVariantMap customRepeat(const QStringList &recurrence, const QDateTime &day,
                                                const QString &zone);
    /// The rule for the custom form's `custom`, keeping the lines of `previous`
    /// that are not rules, such as deleted days.
    Q_INVOKABLE static QStringList customRule(const QVariantMap &custom, const QDateTime &day,
                                              bool allDay, const QString &zone,
                                              const QStringList &previous);
    /// The custom form's `custom` for a start moved `days` on: a weekly rule's
    /// weekdays move with it.
    Q_INVOKABLE static QVariantMap shiftCustom(const QVariantMap &custom, int days);
    /// `recurrence` in words, from the choices or the custom form, or "Custom".
    Q_INVOKABLE static QString describeRepeat(const QStringList &recurrence, const QDateTime &day,
                                              const QString &zone);

    /// Makes a copy of the event, starting at `at`, or at the same time when it
    /// is invalid. An all-day copy keeps to whole days. The copy goes in the
    /// event's calendar if it can be written to, otherwise in `fallbackCalendar`
    /// or the first calendar that can.
    Q_INVOKABLE void duplicate(const QVariantMap &event, const QDateTime &at,
                               const QString &fallbackCalendar);
    /// The copy's details, as duplicate() creates it.
    [[nodiscard]] static EventDraft toCopy(const QVariantMap &event, const QDateTime &start);

    /// Gives the event new times, as dragging it on the grid does.
    Q_INVOKABLE void move(const QVariantMap &event, const QDateTime &from, const QDateTime &to,
                          bool wholeSeries);
    /// A mailto: link to every other guest, with the event's title as subject.
    Q_INVOKABLE static QUrl mailGuests(const QVariantMap &event);
    Q_INVOKABLE void clearError();

Q_SIGNALS:
    void sourceChanged();
    void busyChanged();
    void errorChanged();
    /// Each names the event it was for, since a slow reply can arrive after
    /// the card has moved on to another event.
    void responded(const QString &eventId, const QString &status);
    void removed(const QString &eventId);
    void moved(const QString &eventId);
    void updated(const QString &eventId);
    void duplicated();

private:
    [[nodiscard]] static Event toEvent(const QVariantMap &event);
    void start();
    void finish(const QString &eventId, const QString &error);

    QPointer<CalendarSource> m_source;
    bool m_busy = false;
    QString m_error;
    QString m_errorEventId;
};

} // namespace callie
