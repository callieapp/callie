#include "SubscriptionsController.h"

#include "callie/Subscriptions.h"

#include <QCoreApplication>

using namespace Qt::StringLiterals;

namespace callie {

SubscriptionsController::SubscriptionsController(QObject *parent) : QObject(parent) {}

SubscriptionsController *SubscriptionsController::instance()
{
    static auto *subscribed = new SubscriptionsController(QCoreApplication::instance());
    return subscribed;
}

SubscriptionsController *SubscriptionsController::create(QQmlEngine *, QJSEngine *)
{
    SubscriptionsController *subscribed = instance();
    QJSEngine::setObjectOwnership(subscribed, QJSEngine::CppOwnership);
    return subscribed;
}

void SubscriptionsController::setUp(Subscriptions *subscriptions)
{
    if (m_subscriptions)
        disconnect(m_subscriptions, nullptr, this, nullptr);
    m_subscriptions = subscriptions;
    if (m_subscriptions) {
        connect(m_subscriptions, &CalendarSource::changed, this,
                &SubscriptionsController::calendarsChanged);
        connect(m_subscriptions, &CalendarSource::statusChanged, this,
                &SubscriptionsController::calendarsChanged);
    }
    Q_EMIT calendarsChanged();
}

QVariantList SubscriptionsController::calendars() const
{
    QVariantList list;
    if (!m_subscriptions)
        return list;
    const QVariantList report = m_subscriptions->syncReport();
    const QList<CalendarInfo> calendars = m_subscriptions->calendars();
    for (qsizetype i = 0; i < calendars.size(); ++i) {
        const QVariantMap status = report.value(i).toMap();
        list << QVariantMap{{u"id"_s, calendars.at(i).id},
                            {u"name"_s, calendars.at(i).displayName},
                            {u"color"_s, calendars.at(i).color},
                            {u"link"_s, m_subscriptions->link(calendars.at(i).id)},
                            {u"error"_s, status.value(u"error"_s)},
                            // Null until the first fetch, so QML can tell.
                            {u"fetched"_s, status.value(u"lastSynced"_s).toDateTime().isValid()
                                               ? status.value(u"lastSynced"_s)
                                               : QVariant()}};
    }
    return list;
}

bool SubscriptionsController::accepts(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    return scheme == u"webcal" || scheme == u"webcals";
}

QString SubscriptionsController::add(const QString &link)
{
    if (!m_subscriptions) {
        setError(tr("Subscriptions cannot be changed here."));
        return {};
    }
    QString error;
    const QString id = m_subscriptions->add(link, {}, &error);
    setError(error);
    return id;
}

bool SubscriptionsController::remove(const QString &id)
{
    QString error;
    const bool removed = m_subscriptions && m_subscriptions->remove(id, &error);
    setError(error);
    return removed;
}

void SubscriptionsController::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    Q_EMIT errorChanged();
}

} // namespace callie
