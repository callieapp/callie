#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QVariantList>

namespace callie {

class Subscriptions;

/// The calendars subscribed to by link, for settings and for links opened in
/// a browser: lists them with how their last fetch went, subscribes to a
/// link and stops a subscription.
class SubscriptionsController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Subscribed)
    QML_SINGLETON
    /// {id, name, link, error, fetched} for each subscription; fetched is null
    /// until the first fetch.
    Q_PROPERTY(QVariantList calendars READ calendars NOTIFY calendarsChanged)
    Q_PROPERTY(bool available READ available NOTIFY calendarsChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    static SubscriptionsController *instance();
    static SubscriptionsController *create(QQmlEngine *, QJSEngine *);

    void setUp(Subscriptions *subscriptions);

    [[nodiscard]] QVariantList calendars() const;
    [[nodiscard]] bool available() const { return !m_subscriptions.isNull(); }
    [[nodiscard]] QString error() const { return m_error; }

    /// Whether `url` is a link to subscribe to, as a browser hands them over.
    Q_INVOKABLE static bool accepts(const QUrl &url);
    /// Subscribes to `link`; returns its id, or empty with error set.
    Q_INVOKABLE QString add(const QString &link);
    Q_INVOKABLE bool remove(const QString &id);

Q_SIGNALS:
    void calendarsChanged();
    void errorChanged();

private:
    explicit SubscriptionsController(QObject *parent);
    void setError(const QString &error);

    QPointer<Subscriptions> m_subscriptions;
    QString m_error;
};

} // namespace callie
