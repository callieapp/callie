# A development tool for designing themes: it renders every token and component
# from the active theme and reloads when the theme file changes. Not installed.
qt_add_executable(callie-gallery
    main.cpp
)

qt_add_qml_module(callie-gallery
    URI Callie.Gallery
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Callie/Gallery
    # Lets qmllint find Callie.Ui, which lives elsewhere in the build tree.
    IMPORT_PATH ${CMAKE_BINARY_DIR}
    DEPENDENCIES Callie.Ui
    QML_FILES
        Gallery.qml
)

target_link_libraries(callie-gallery PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    callieui
    callieuiplugin
)

if(CALLIE_LIVE_QML)
    target_compile_definitions(callie-gallery PRIVATE
        CALLIE_LIVE_QML
        CALLIE_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        CALLIE_BINARY_DIR="${CMAKE_BINARY_DIR}"
    )
endif()
