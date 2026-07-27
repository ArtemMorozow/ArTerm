include(FetchContent)

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL)

FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)

# One binary for the whole suite: the tests are pure logic and link in
# milliseconds, so there is nothing to gain from splitting them up.
add_executable(arterm_tests
    tst_base64.cpp
    tst_boxdrawing.cpp
    tst_charwidth.cpp
    tst_dispatch.cpp
    tst_hoststore.cpp
    tst_json.cpp
    tst_colorscheme.cpp
    tst_keyencoder.cpp
    tst_screen.cpp
    tst_signal.cpp
    tst_terminal.cpp
    tst_vtparser.cpp)

target_link_libraries(arterm_tests PRIVATE arterm_core Catch2::Catch2WithMain)

catch_discover_tests(arterm_tests)
