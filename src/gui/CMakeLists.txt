qt_add_executable(callie-gui
    main.cpp
)

target_compile_definitions(callie-gui PRIVATE
    CALLIE_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(callie-gui PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    callieui
    callieuiplugin
)

if(CALLIE_LIVE_QML)
    target_compile_definitions(callie-gui PRIVATE
        CALLIE_LIVE_QML
        CALLIE_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        CALLIE_BINARY_DIR="${CMAKE_BINARY_DIR}"
    )
endif()

install(TARGETS callie-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
