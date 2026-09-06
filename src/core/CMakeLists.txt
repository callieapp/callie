# Deliberately not a QML module. The CLI links this, and pulling QtQml in would
# cost it startup time for no benefit. QML exposure happens in src/ui.
add_library(calliecore STATIC
    callie/Event.h
    callie/CalendarSource.h
    callie/SampleSource.h
    SampleSource.cpp
    callie/EventModel.h
    EventModel.cpp
)

target_include_directories(calliecore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(calliecore
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Sql
)
