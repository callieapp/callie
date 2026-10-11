# The commands are a library of their own so tests can run them on sample data.
add_library(calliecli STATIC
    Commands.h
    Commands.cpp
    Completion.h
    Completion.cpp
    UpdateHint.h
    UpdateHint.cpp
    Upgrade.h
    Upgrade.cpp
)
target_include_directories(calliecli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(calliecli PUBLIC Qt6::Core calliecore)

qt_add_executable(callie
    main.cpp
)

# Printed by callie completions, and installed where each shell looks.
qt_add_resources(callie completions
    PREFIX /completions
    BASE ${PROJECT_SOURCE_DIR}/data/completions
    FILES
        ${PROJECT_SOURCE_DIR}/data/completions/callie.bash
        ${PROJECT_SOURCE_DIR}/data/completions/_callie
        ${PROJECT_SOURCE_DIR}/data/completions/callie.fish
)
install(FILES ${PROJECT_SOURCE_DIR}/data/completions/callie.bash
    DESTINATION ${CMAKE_INSTALL_DATADIR}/bash-completion/completions RENAME callie)
install(FILES ${PROJECT_SOURCE_DIR}/data/completions/_callie
    DESTINATION ${CMAKE_INSTALL_DATADIR}/zsh/site-functions)
install(FILES ${PROJECT_SOURCE_DIR}/data/completions/callie.fish
    DESTINATION ${CMAKE_INSTALL_DATADIR}/fish/vendor_completions.d)

target_compile_definitions(callie PRIVATE
    CALLIE_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(callie PRIVATE
    Qt6::Core
    calliecore
    calliecli
)

install(TARGETS callie RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
