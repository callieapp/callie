#pragma once

#include "callie/EventModel.h"

#include <QQmlEngine>

namespace callie {

/// Exposes EventModel to QML without QML types leaking into the core library,
/// so the CLI can link the core without pulling in QtQml.
struct EventModelForeign
{
    Q_GADGET
    QML_FOREIGN(callie::EventModel)
    QML_NAMED_ELEMENT(EventModel)
};

} // namespace callie
