#pragma once

#include "callie/CalendarSource.h"
#include "callie/EventModel.h"
#include "callie/MonthModel.h"
#include "callie/Settings.h"
#include "callie/TodayModel.h"

#include <QCoreApplication>
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

struct TodayModelForeign
{
    Q_GADGET
    QML_FOREIGN(callie::TodayModel)
    QML_NAMED_ELEMENT(TodayModel)
};

struct MonthModelForeign
{
    Q_GADGET
    QML_FOREIGN(callie::MonthModel)
    QML_NAMED_ELEMENT(MonthModel)
};

/// The user's preferences as a QML singleton. main.cpp picks the instance;
/// without one, such as in the gallery, it is the user's own settings file.
struct SettingsForeign
{
    Q_GADGET
    QML_FOREIGN(callie::Settings)
    QML_NAMED_ELEMENT(Settings)
    QML_SINGLETON

public:
    static callie::Settings *create(QQmlEngine *, QJSEngine *)
    {
        if (!s_instance)
            s_instance =
                new callie::Settings(callie::Settings::defaultPath(), QCoreApplication::instance());
        QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
        return s_instance;
    }

    static inline callie::Settings *s_instance = nullptr;
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
