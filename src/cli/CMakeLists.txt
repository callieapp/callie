qt_add_executable(callie
    main.cpp
)

target_compile_definitions(callie PRIVATE
    CALLIE_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(callie PRIVATE
    Qt6::Core
    calliecore
)

install(TARGETS callie RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
