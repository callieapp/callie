#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QVariantMap>

namespace callie {

class CalDavSource;

/// The calendars of CalDAV accounts, for settings: lists them by account, makes
/// one on the server and deletes one there. main.cpp sets it up.
class ServerCalendarsController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ServerCalendars)
    QML_SINGLETON
    /// For each account id, {id, name, color} of each of its calendars.
    Q_PROPERTY(QVariantMap calendars READ calendars NOTIFY calendarsChanged)
    /// A calendar is being made or deleted.
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    /// What went wrong, and the account it went wrong in.
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString errorAccount READ errorAccount NOTIFY stateChanged)

public:
    static ServerCalendarsController *instance();
    static ServerCalendarsController *create(QQmlEngine *, QJSEngine *);

    void setUp(CalDavSource *source);

    [[nodiscard]] QVariantMap calendars() const;
    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] QString errorAccount() const { return m_errorAccount; }

    /// Makes a calendar on the account's server; `made` says when it is there.
    Q_INVOKABLE void add(const QString &accountId, const QString &name);
    /// Deletes a calendar on its server.
    Q_INVOKABLE void remove(const QString &calendarId);

Q_SIGNALS:
    void calendarsChanged();
    void stateChanged();
    void made(const QString &accountId);

private:
    explicit ServerCalendarsController(QObject *parent);
    void finish(const QString &accountId, const QString &error);

    QPointer<CalDavSource> m_source;
    bool m_busy = false;
    QString m_error;
    QString m_errorAccount;
};

} // namespace callie
