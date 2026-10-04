find_package(Qt6 REQUIRED COMPONENTS Test)

# TZ is pinned so that wall-clock assertions do not depend on the host zone or
# on whether a DST transition falls inside the test window.
function(callie_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE Qt6::Test calliecore)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "TZ=UTC;QT_QPA_PLATFORM=offscreen")
endfunction()

callie_add_test(tst_eventmodel)
