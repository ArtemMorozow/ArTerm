find_package(Qt6 REQUIRED COMPONENTS Test)

# Each test is a self-contained QTest binary linking against arterm_core.
set(ARTERM_TESTS
    tst_vtparser
    tst_screen
    tst_terminal
    tst_keyencoder
    tst_charwidth
    tst_colorscheme
    tst_hoststore)

foreach(test IN LISTS ARTERM_TESTS)
    qt_add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE arterm_core Qt6::Test)
    set_target_properties(${test} PROPERTIES MACOSX_BUNDLE OFF)
    add_test(NAME ${test} COMMAND ${test})

    # The tests never open a window; running them headless keeps CI simple.
    set_tests_properties(${test} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
