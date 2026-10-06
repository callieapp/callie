#pragma once

#include "callie/CalendarSource.h"

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantMap>

namespace callie {

/// What the event details card can do to an event: answer the invitation,
/// delete it, or write to its guests. Events come as EventModel's row maps.
class EventActions : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(callie::CalendarSource *source MEMBER m_source NOTIFY sourceChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    using QObject::QObject;

    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] QString error() const { return m_error; }

    /// "accepted", "tentative" or "declined".
    Q_INVOKABLE void respond(const QVariantMap &event, const QString &status, bool wholeSeries);
    Q_INVOKABLE void remove(const QVariantMap &event, bool wholeSeries);
    /// A mailto: link to every other guest, with the event's title as subject.
    Q_INVOKABLE static QUrl mailGuests(const QVariantMap &event);
    Q_INVOKABLE void clearError();

Q_SIGNALS:
    void sourceChanged();
    void busyChanged();
    void errorChanged();
    void responded(const QString &status);
    void removed();

private:
    [[nodiscard]] static Event toEvent(const QVariantMap &event);
    void start();
    void finish(const QString &error);

    QPointer<CalendarSource> m_source;
    bool m_busy = false;
    QString m_error;
};

} // namespace callie
