# QT_QML_SINGLETON_TYPE must be set before the file is added to the module,
# otherwise qmldir omits the "singleton" keyword and Theme.* silently
# evaluates to undefined at runtime.
set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# The QML files sit at this module's root, which is what Qt expects. Keeping
# them in a subdirectory would require a hand-written qmldir (policy QTP0004).
qt_add_qml_module(callieui
    URI Callie.Ui
    VERSION 1.0
    STATIC
    # qmllint and qmlls expect the output path to mirror the module URI.
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Callie/Ui
    SOURCES
        EventModelForeign.h
    QML_FILES
        Main.qml
        WeekView.qml
        EventBlock.qml
        NavButton.qml
        PillButton.qml
        Theme.qml
)


target_link_libraries(callieui
    PUBLIC
        Qt6::Quick
        calliecore
)
