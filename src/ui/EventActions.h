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
