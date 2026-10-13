cmake_minimum_required(VERSION 3.28)

project(callie
    VERSION 0.4.0
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
# A development build runs beside an installed Callie, with its own app id,
# folders and keyring entries, so the two never share accounts or a cache.
option(CALLIE_DEVEL "Give this build its own identity, apart from an installed Callie" OFF)

# `callie` in a terminal opens a TUI built with FTXUI. CALLIE_TUI=AUTO leaves it
# out when FTXUI is missing; packages and CI ask for ON. FTXUI says only its own
# major version is compatible, so the minimum is checked here rather than asked
# of find_package.
set(CALLIE_TUI "AUTO" CACHE STRING "Build the TUI: ON, OFF or AUTO, when FTXUI is there")
set_property(CACHE CALLIE_TUI PROPERTY STRINGS ON OFF AUTO)
if(NOT CALLIE_TUI STREQUAL "OFF")
    find_package(ftxui CONFIG QUIET)
endif()
if(ftxui_FOUND AND ftxui_VERSION VERSION_LESS 5)
    set(ftxui_FOUND FALSE)
endif()
if(ftxui_FOUND)
    message(STATUS "Callie: TUI with FTXUI ${ftxui_VERSION}")
elseif(CALLIE_TUI STREQUAL "ON")
    message(FATAL_ERROR "CALLIE_TUI is ON, but FTXUI 5 or later is not installed")
else()
    message(STATUS "Callie: no TUI")
endif()

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
