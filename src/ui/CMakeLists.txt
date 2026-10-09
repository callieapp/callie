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
        AccountsController.h
        AccountsController.cpp
        Clock.h
        Clock.cpp
        Composer.h
        Composer.cpp
        EventActions.h
        EventActions.cpp
        KeyRouter.h
        KeyRouter.cpp
        EventModelForeign.h
        LiveQml.h
        NotificationServer.h
        NotificationServer.cpp
        Reminders.h
        Reminders.cpp
        SingleInstance.h
        SingleInstance.cpp
        StartAtLogin.h
        StartAtLogin.cpp
        LiveQml.cpp
        SupportActions.h
        SupportActions.cpp
        ReleaseInfo.h
        ReleaseInfo.cpp
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
        Stripes.qml
        CalendarMark.qml
        PillButton.qml
        StickerButton.qml
        DangerButton.qml
        Glyph.qml
        Logo.qml
        AboutDialog.qml
        WhatsNewDialog.qml
        WindowControls.qml
        ResizeFrame.qml
        MenuEntry.qml
        MenuCard.qml
        MoveChoice.qml
        EventMover.qml
        KeyCap.qml
        Scrim.qml
        WhichKey.qml
        KeyHelp.qml
        AllDayChip.qml
        Tip.qml
        Toggle.qml
        ZonePicker.qml
        TimePicker.qml
        DatePicker.qml
        EventEditor.qml
        InvitesTray.qml
        Field.qml
        LookEditor.qml
        SearchPopup.qml
        UndoToast.qml
        SettingsDialog.qml
        AccountsSection.qml
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
        Qt6::DBus
        calliecore
)
