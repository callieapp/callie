#include "callie/Event.h"

#include "callie/Places.h"

namespace callie {

QUrl Event::joinUrl() const
{
    if (!conferenceUrl.isEmpty())
        return conferenceUrl;
    return places::findCallLink(location + u'\n' + description);
}

} // namespace callie
