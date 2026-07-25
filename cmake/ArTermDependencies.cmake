# Third-party dependency resolution.
#
# macOS:  brew install qt libssh2
#         cmake -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix libssh2)"
# Linux:  apt install qt6-base-dev libssh2-1-dev   (used for CI smoke builds)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network Svg)

qt_standard_project_setup()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

# ---------------------------------------------------------------------------
# libssh2 - prefer the CMake package config, fall back to pkg-config, and as a
# last resort search the usual Homebrew prefixes by hand.
# ---------------------------------------------------------------------------
find_package(Libssh2 CONFIG QUIET)

if(TARGET Libssh2::libssh2_shared)
    add_library(arterm::libssh2 ALIAS Libssh2::libssh2_shared)
elseif(TARGET Libssh2::libssh2_static)
    add_library(arterm::libssh2 ALIAS Libssh2::libssh2_static)
elseif(TARGET libssh2::libssh2)
    add_library(arterm::libssh2 ALIAS libssh2::libssh2)
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBSSH2 IMPORTED_TARGET libssh2)
    endif()

    if(TARGET PkgConfig::LIBSSH2)
        add_library(arterm::libssh2 ALIAS PkgConfig::LIBSSH2)
    else()
        find_path(LIBSSH2_INCLUDE_DIR
            NAMES libssh2.h
            HINTS /opt/homebrew/opt/libssh2/include /usr/local/opt/libssh2/include)
        find_library(LIBSSH2_LIBRARY
            NAMES ssh2 libssh2
            HINTS /opt/homebrew/opt/libssh2/lib /usr/local/opt/libssh2/lib)

        if(NOT LIBSSH2_INCLUDE_DIR OR NOT LIBSSH2_LIBRARY)
            message(FATAL_ERROR
                "libssh2 was not found.\n"
                "  macOS: brew install libssh2 && "
                "cmake -B build -DCMAKE_PREFIX_PATH=\"$(brew --prefix qt);$(brew --prefix libssh2)\"\n"
                "  Linux: sudo apt install libssh2-1-dev")
        endif()

        add_library(arterm_libssh2_imported UNKNOWN IMPORTED)
        set_target_properties(arterm_libssh2_imported PROPERTIES
            IMPORTED_LOCATION "${LIBSSH2_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LIBSSH2_INCLUDE_DIR}")
        add_library(arterm::libssh2 ALIAS arterm_libssh2_imported)
    endif()
endif()

find_package(Threads REQUIRED)
