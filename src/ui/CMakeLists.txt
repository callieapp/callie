# The QML files sit at this module's root, which is what Qt expects. Keeping
# them in a subdirectory would require a hand-written qmldir (policy QTP0004).
qt_add_qml_module(callieui
    URI Callie.Ui
    VERSION 1.0
    STATIC
    # qmllint and qmlls expect the output path to mirror the module URI.
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Callie/Ui
    # Theme exposes QColor properties; older qmllint only maps them to QML's
    # color type when QtQuick is a declared dependency.
    DEPENDENCIES QtQuick
    SOURCES
        Clock.h
        Clock.cpp
        Composer.h
        Composer.cpp
        EventActions.h
        EventActions.cpp
        EventModelForeign.h
        LiveQml.h
        LiveQml.cpp
        SupportActions.h
        SupportActions.cpp
        ThemeController.h
        ThemeController.cpp
        ThemesController.h
        ThemesController.cpp
        WindowButtons.h
        WindowButtons.cpp
        Views.h
        Views.cpp
    QML_FILES
        Main.qml
        WeekView.qml
        MonthView.qml
        AgendaView.qml
        QuickAddPopup.qml
        Sidebar.qml
        EventBlock.qml
        EventTip.qml
        EventDetails.qml
        SoftShadow.qml
        PillButton.qml
        StickerButton.qml
        Glyph.qml
        Logo.qml
        WindowControls.qml
        ResizeFrame.qml
        MenuEntry.qml
        Tip.qml
        Toggle.qml
        ZonePicker.qml
        SettingsDialog.qml
        ThemeEditor.qml
)

qt_add_resources(callieui "callie_logo"
    PREFIX "/callie"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES "${PROJECT_SOURCE_DIR}/assets/logo.png"
)

target_include_directories(callieui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(callieui
    PUBLIC
        Qt6::Quick
        calliecore
)
