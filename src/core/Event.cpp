#include "callie/Event.h"

#include "callie/Places.h"

namespace callie {

QUrl Event::joinUrl() const
{
    // The provider's link comes from whoever sent the invitation, so only a web
    // link is opened, never a file or share.
    if (conferenceUrl.isValid() &&
        (conferenceUrl.scheme() == u"https" || conferenceUrl.scheme() == u"http"))
        return conferenceUrl;
    return places::findCallLink(location + u'\n' + description);
}

} // namespace callie
