#include "callie/Places.h"

#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace callie::places {

namespace {

bool isCall(const QUrl &url)
{
    const QString host = url.host().toLower();
    const QString path = url.path();
    if (host == u"zoom.us" || host.endsWith(u".zoom.us"))
        return path.startsWith(u"/j/") || path.startsWith(u"/my/") || path.startsWith(u"/s/");
    if (host == u"meet.google.com")
        return path.size() > 1;
    if (host == u"teams.microsoft.com")
        return path.startsWith(u"/l/meetup-join") || path.startsWith(u"/meet/");
    if (host == u"teams.live.com")
        return path.startsWith(u"/meet");
    if (host.endsWith(u".webex.com"))
        return path.contains(u"/meet") || path.contains(u"/j.php") || path.contains(u"/join");
    return false;
}

} // namespace

QUrl findCallLink(const QString &text)
{
    static const QRegularExpression link(uR"(https?://[^\s<>"']+)"_s,
                                         QRegularExpression::CaseInsensitiveOption);
    for (auto it = link.globalMatch(text); it.hasNext();) {
        QString found = it.next().captured();
        // Sentence punctuation after a link is not part of it.
        while (!found.isEmpty() && QStringView(u".,;:!?)]").contains(found.back()))
            found.chop(1);
        const QUrl url(found);
        if (url.isValid() && isCall(url))
            return url;
    }
    return {};
}

QUrl mapUrl(const QString &app, const QString &location)
{
    const QString place = location.simplified();
    const QUrl asIs(place);
    if (asIs.isValid() && (asIs.scheme() == u"https" || asIs.scheme() == u"http"))
        return asIs;
    const QString query = QString::fromLatin1(QUrl::toPercentEncoding(place));
    if (app == u"google")
        return QUrl(u"https://www.google.com/maps/search/?api=1&query="_s + query);
    if (app == u"osm")
        return QUrl(u"https://www.openstreetmap.org/search?query="_s + query);
    if (app == u"apple")
        return QUrl(u"https://maps.apple.com/?q="_s + query);
    return QUrl(u"geo:0,0?q="_s + query);
}

} // namespace callie::places
