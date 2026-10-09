#pragma once

#include "PlaceSearch.h"
#include "callie/CalendarSource.h"
#include "callie/ContactBook.h"
#include "callie/EventModel.h"
#include "callie/InvitesModel.h"
#include "callie/MonthModel.h"
#include "callie/SearchModel.h"
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

struct InvitesModelForeign
{
    Q_GADGET
    QML_FOREIGN(callie::InvitesModel)
    QML_NAMED_ELEMENT(InvitesModel)
};

struct SearchModelForeign
{
    Q_GADGET
    QML_FOREIGN(callie::SearchModel)
    QML_NAMED_ELEMENT(SearchModel)
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

struct TimesForeign
{
    Q_GADGET
    QML_FOREIGN(callie::Times)
    QML_NAMED_ELEMENT(Times)
    QML_UNCREATABLE("Times come from Settings.times.")
};

/// People to suggest as guests, as a QML singleton. main.cpp picks the instance;
/// without one, such as in tests, it is an empty book in memory.
struct ContactBookForeign
{
    Q_GADGET
    QML_FOREIGN(callie::ContactBook)
    QML_NAMED_ELEMENT(Contacts)
    QML_SINGLETON

public:
    static callie::ContactBook *create(QQmlEngine *, QJSEngine *)
    {
        if (!s_instance)
            s_instance = new callie::ContactBook({}, QCoreApplication::instance());
        QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
        return s_instance;
    }

    static inline callie::ContactBook *s_instance = nullptr;
};

/// Places to suggest for an event, as a QML singleton. main.cpp picks the
/// instance; without one, such as in tests, only the user's own are suggested.
struct PlaceSearchForeign
{
    Q_GADGET
    QML_FOREIGN(callie::PlaceSearch)
    QML_NAMED_ELEMENT(Places)
    QML_SINGLETON

public:
    static callie::PlaceSearch *create(QQmlEngine *, QJSEngine *)
    {
        if (!s_instance)
            s_instance = new callie::PlaceSearch(QCoreApplication::instance());
        QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
        return s_instance;
    }

    static inline callie::PlaceSearch *s_instance = nullptr;
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
