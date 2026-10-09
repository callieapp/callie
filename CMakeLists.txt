cmake_minimum_required(VERSION 3.28)

project(callie
    VERSION 0.2.1
    DESCRIPTION "An elegant calendar for Linux"
    HOMEPAGE_URL "https://callieapp.org"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

set(CALLIE_APP_ID "org.callieapp.Callie" CACHE STRING "Reverse-DNS application id")

option(CALLIE_BUILD_TESTS "Build the unit tests" ON)
option(CALLIE_WERROR "Treat compiler warnings as errors" OFF)
# Builds in the source tree's path, so it is for development builds only.
option(CALLIE_LIVE_QML "Add --live-qml to the app and gallery to reload QML from src/" OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CALLIE_WERROR)
        add_compile_options(-Werror)
    endif()
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network NetworkAuth Sql DBus)
qt_standard_project_setup(REQUIRES 6.8)

if(CALLIE_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(src/core)
add_subdirectory(src/ui)
add_subdirectory(src/gui)
add_subdirectory(src/cli)
add_subdirectory(src/gallery)
add_subdirectory(data)

# Built-in themes are compiled in; installed copies are a starting point for
# people writing their own.
install(DIRECTORY themes/ DESTINATION ${CMAKE_INSTALL_DATADIR}/callie/themes
    FILES_MATCHING PATTERN "*.toml")

if(CALLIE_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Formatting is driven by one script so that CMake, CI and a local run agree.
# The script is development tooling and is left out of release tarballs.
if(EXISTS "${CMAKE_SOURCE_DIR}/scripts/format.sh")
    add_custom_target(format
        COMMAND "${CMAKE_SOURCE_DIR}/scripts/format.sh" fix
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Formatting C++ and QML sources"
        VERBATIM
    )

    add_custom_target(format-check
        COMMAND "${CMAKE_SOURCE_DIR}/scripts/format.sh" check
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Checking C++ and QML formatting"
        VERBATIM
    )
endif()
