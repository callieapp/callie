#include "PlaceSearch.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrlQuery>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

constexpr int kMinLetters = 3;
constexpr int kDaysAround = 365;

// "Name, 12 Street, City, Country", without what is missing or repeated.
QString labelOf(const QJsonObject &place)
{
    QStringList parts;
    const auto add = [&parts](const QString &part) {
        if (!part.isEmpty() && !parts.contains(part))
            parts << part;
    };
    add(place[u"name"].toString());
    const QString street = place[u"street"].toString();
    const QString number = place[u"housenumber"].toString();
    add(number.isEmpty() ? street : street.isEmpty() ? number : number + u' ' + street);
    add(place[u"city"].toString());
    add(place[u"country"].toString());
    return parts.join(u", "_s);
}

} // namespace

PlaceSearch::PlaceSearch(QObject *parent) : QObject(parent)
{
    m_delay.setSingleShot(true);
    m_delay.setInterval(300);
    connect(&m_delay, &QTimer::timeout, this, &PlaceSearch::ask);
}

void PlaceSearch::setSource(CalendarSource *source)
{
    if (m_source == source)
        return;
    if (m_source)
        disconnect(m_source, nullptr, this, nullptr);
    m_source = source;
    m_pastRead = false;
    if (m_source)
        connect(m_source, &CalendarSource::changed, this, [this] { m_pastRead = false; });
    Q_EMIT sourceChanged();
}

void PlaceSearch::search(const QString &text)
{
    m_text = text.trimmed();
    if (m_reply)
        m_reply->abort();
    if (m_text.isEmpty()) {
        clear();
        return;
    }
    if (!m_pastRead && m_source) {
        // Read once, when first wanted, most used first.
        m_pastRead = true;
        const QDateTime now = QDateTime::currentDateTime();
        QHash<QString, int> uses;
        for (const Event &e :
             m_source->eventsBetween(now.addDays(-kDaysAround), now.addDays(kDaysAround),
                                     QTimeZone::systemTimeZone())) {
            const QString place = e.location.simplified();
            if (!place.isEmpty() && !place.startsWith(u"http"_s))
                ++uses[place];
        }
        m_past = uses.keys();
        std::sort(m_past.begin(), m_past.end(), [&uses](const QString &a, const QString &b) {
            return uses[a] != uses[b] ? uses[a] > uses[b] : a < b;
        });
    }
    m_pastMatches.clear();
    for (const QString &place : std::as_const(m_past)) {
        if (place.contains(m_text, Qt::CaseInsensitive) && m_pastMatches.size() < kLimit)
            m_pastMatches << place;
    }
    show({});
    if (m_text.size() >= kMinLetters && m_server.isValid())
        m_delay.start();
    else
        m_delay.stop();
}

void PlaceSearch::clear()
{
    m_delay.stop();
    if (m_reply)
        m_reply->abort();
    m_text.clear();
    m_pastMatches.clear();
    if (m_results.isEmpty())
        return;
    m_results.clear();
    Q_EMIT resultsChanged();
}

void PlaceSearch::ask()
{
    QUrl url = m_server.resolved(QUrl(u"api/"_s));
    QUrlQuery query;
    query.addQueryItem(u"q"_s, m_text);
    query.addQueryItem(u"limit"_s, QString::number(kLimit));
    url.setQuery(query);
    QNetworkRequest request(url);
    // Photon's public server asks callers to say who they are.
    request.setHeader(
        QNetworkRequest::UserAgentHeader,
        u"Callie/%1 (https://callieapp.org)"_s.arg(QCoreApplication::applicationVersion()));
    QNetworkReply *reply = m_network.get(request);
    m_reply = reply;
    const QString asked = m_text;
    connect(reply, &QNetworkReply::finished, this, [this, reply, asked] {
        reply->deleteLater();
        // An answer to text since changed, or a failure, shows nothing new.
        if (reply->error() != QNetworkReply::NoError || asked != m_text)
            return;
        QStringList remote;
        const QJsonArray features =
            QJsonDocument::fromJson(reply->readAll()).object()[u"features"].toArray();
        for (const QJsonValue &feature : features) {
            const QString label = labelOf(feature[u"properties"].toObject());
            if (!label.isEmpty() && !remote.contains(label))
                remote << label;
        }
        show(remote);
    });
}

void PlaceSearch::show(const QStringList &remote)
{
    QVariantList results;
    for (const QString &place : std::as_const(m_pastMatches))
        results << QVariantMap{{u"label"_s, place}, {u"past"_s, true}};
    for (const QString &place : remote) {
        if (!m_pastMatches.contains(place))
            results << QVariantMap{{u"label"_s, place}, {u"past"_s, false}};
    }
    if (results == m_results)
        return;
    m_results = results;
    Q_EMIT resultsChanged();
}

} // namespace callie
