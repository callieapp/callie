#pragma once

#include <QString>
#include <QUrl>

namespace callie::places {

/// The first video call link in free text, such as an event's place or notes:
/// Zoom, Google Meet, Microsoft Teams or Webex. Empty when there is none.
[[nodiscard]] QUrl findCallLink(const QString &text);

/// Where `app` shows `location`: "system" for a geo: link the desktop's own
/// map app opens, "google", "osm" (OpenStreetMap) or "apple". A location that
/// is itself a web address is opened as it is.
[[nodiscard]] QUrl mapUrl(const QString &app, const QString &location);

} // namespace callie::places
