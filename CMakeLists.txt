cmake_minimum_required(VERSION 3.28)

project(callie
    VERSION 0.1.0
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

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network Sql DBus)
qt_standard_project_setup(REQUIRES 6.6)

add_subdirectory(src/core)
add_subdirectory(src/ui)
add_subdirectory(src/gui)
add_subdirectory(src/cli)
add_subdirectory(data)
