# The commands are a library of their own so tests can run them on sample data.
add_library(calliecli STATIC
    Commands.h
    Commands.cpp
)
target_include_directories(calliecli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(calliecli PUBLIC Qt6::Core calliecore)

qt_add_executable(callie
    main.cpp
)

target_compile_definitions(callie PRIVATE
    CALLIE_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(callie PRIVATE
    Qt6::Core
    calliecore
    calliecli
)

install(TARGETS callie RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
