#pragma once

#include "callie/CalendarSource.h"
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

/// Lets QML name the source type; sources are created in C++ only.
struct CalendarSourceForeign
{
    Q_GADGET
    QML_FOREIGN(callie::CalendarSource)
    QML_NAMED_ELEMENT(CalendarSource)
    QML_UNCREATABLE("Calendar sources are created in C++.")
};

} // namespace callie
