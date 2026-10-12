#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QVariantList>

namespace callie {

class LocalCalendars;

/// The calendars on this computer, for settings: lists them, makes a new one
/// and moves one to the trash. main.cpp sets it up; without that, as with
/// sample data, it lists nothing and says why.
class LocalController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Local)
    QML_SINGLETON
    /// {id, name, color} for each calendar.
    Q_PROPERTY(QVariantList calendars READ calendars NOTIFY calendarsChanged)
    Q_PROPERTY(bool available READ available NOTIFY calendarsChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    static LocalController *instance();
    static LocalController *create(QQmlEngine *, QJSEngine *);

    void setUp(LocalCalendars *calendars);

    [[nodiscard]] QVariantList calendars() const;
    [[nodiscard]] bool available() const { return !m_calendars.isNull(); }
    [[nodiscard]] QString error() const { return m_error; }

    /// Makes a calendar and returns its id, or empty with error set.
    Q_INVOKABLE QString add(const QString &name);
    Q_INVOKABLE bool remove(const QString &id);

Q_SIGNALS:
    void calendarsChanged();
    void errorChanged();

private:
    explicit LocalController(QObject *parent);
    void setError(const QString &error);

    QPointer<LocalCalendars> m_calendars;
    QString m_error;
};

} // namespace callie
