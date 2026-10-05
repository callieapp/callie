qt_add_executable(callie-gui
    main.cpp
)

target_compile_definitions(callie-gui PRIVATE
    CALLIE_VERSION="${PROJECT_VERSION}"
    CALLIE_APP_ID="${CALLIE_APP_ID}"
)

target_link_libraries(callie-gui PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    callieui
    callieuiplugin
)

install(TARGETS callie-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
