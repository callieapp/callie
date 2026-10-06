find_package(Qt6Keychain REQUIRED)
find_package(tomlplusplus REQUIRED)
find_package(KF6CalendarCore REQUIRED)

# Google OAuth client. Explicit values win; otherwise the JSON downloaded from
# Google Cloud is read. The values land in a generated header rather than in
# compile definitions, so they stay out of compile_commands.json.
set(CALLIE_GOOGLE_CREDENTIALS_FILE "${PROJECT_SOURCE_DIR}/google-oauth-credentials.json"
    CACHE FILEPATH "Google OAuth client JSON for a Desktop app client")
set(CALLIE_GOOGLE_CLIENT_ID "" CACHE STRING "Google OAuth client id")
set(CALLIE_GOOGLE_CLIENT_SECRET "" CACHE STRING "Google OAuth client secret")

set(callie_google_client_id "${CALLIE_GOOGLE_CLIENT_ID}")
set(callie_google_client_secret "${CALLIE_GOOGLE_CLIENT_SECRET}")
if(callie_google_client_id STREQUAL "" AND EXISTS "${CALLIE_GOOGLE_CREDENTIALS_FILE}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CALLIE_GOOGLE_CREDENTIALS_FILE}")
    file(READ "${CALLIE_GOOGLE_CREDENTIALS_FILE}" callie_google_json)
    string(JSON callie_google_client_id ERROR_VARIABLE callie_google_error
        GET "${callie_google_json}" installed client_id)
    if(NOT callie_google_error)
        string(JSON callie_google_client_secret ERROR_VARIABLE callie_google_error
            GET "${callie_google_json}" installed client_secret)
    endif()
    if(callie_google_error)
        message(FATAL_ERROR "${CALLIE_GOOGLE_CREDENTIALS_FILE} is not a Desktop app client: "
                            "${callie_google_error}")
    endif()
endif()

# The values are pasted into C++ string literals, so anything outside the
# character set Google uses is rejected rather than escaped.
foreach(value IN ITEMS "${callie_google_client_id}" "${callie_google_client_secret}")
    if(NOT value MATCHES "^[A-Za-z0-9._-]*$")
        message(FATAL_ERROR "Google OAuth client id or secret contains unexpected characters")
    endif()
endforeach()

if(callie_google_client_id STREQUAL "")
    message(STATUS "Google OAuth client: not configured, set it at runtime to use Google accounts")
else()
    message(STATUS "Google OAuth client: configured")
endif()

configure_file(GoogleBuildConfig.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/callie/GoogleBuildConfig.h" @ONLY)

# Deliberately not a QML module. The CLI links this, and pulling QtQml in would
# cost it startup time for no benefit. QML exposure happens in src/ui.
add_library(calliecore STATIC
    callie/Event.h
    callie/Logging.h
    Logging.cpp
    callie/LogFile.h
    LogFile.cpp
    callie/Diagnostics.h
    Diagnostics.cpp
    callie/CalendarSource.h
    callie/SampleSource.h
    SampleSource.cpp
    callie/EventModel.h
    EventModel.cpp
    callie/TodayModel.h
    TodayModel.cpp
    callie/MonthModel.h
    MonthModel.cpp
    callie/Account.h
    callie/AccountStore.h
    AccountStore.cpp
    callie/AccountManager.h
    AccountManager.cpp
    callie/TokenStore.h
    TokenStore.cpp
    callie/GoogleClientConfig.h
    GoogleClientConfig.cpp
    callie/GoogleAuth.h
    GoogleAuth.cpp
    callie/GoogleCalendarApi.h
    GoogleCalendarApi.cpp
    callie/GoogleTokenProvider.h
    GoogleTokenProvider.cpp
    callie/GoogleCache.h
    GoogleCache.cpp
    callie/GoogleSync.h
    GoogleSync.cpp
    callie/GoogleRecurrence.h
    GoogleRecurrence.cpp
    callie/GoogleSource.h
    GoogleSource.cpp
    callie/Color.h
    Color.cpp
    callie/ThemeSpec.h
    callie/ThemeLoader.h
    ThemeLoader.cpp
    callie/Settings.h
    Settings.cpp
    callie/SignInPage.h
    SignInPage.cpp
)

# Built-in themes are compiled in, so the app works uninstalled and a broken
# install can never leave it without its default look.
qt_add_resources(calliecore "callie_themes"
    PREFIX "/callie"
    BASE "${PROJECT_SOURCE_DIR}"
    FILES
        "${PROJECT_SOURCE_DIR}/themes/callie.toml"
        "${PROJECT_SOURCE_DIR}/assets/logo.svg"
)

# The QML module in src/ui exposes EventModel as a foreign type. Its registrar
# can only describe the type if this target publishes its metatypes.
qt_extract_metatypes(calliecore)

target_include_directories(calliecore
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(calliecore
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Sql
    PRIVATE
        Qt6::NetworkAuth
        Qt6Keychain::Qt6Keychain
        tomlplusplus::tomlplusplus
        KF6::CalendarCore
)
