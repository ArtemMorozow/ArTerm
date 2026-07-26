# Third-party dependency resolution.
#
# ArTerm is a macOS application built against the system frameworks; libssh2 is
# the only third-party library it needs.
#
#   brew install libssh2
#   cmake -B build -DCMAKE_PREFIX_PATH="$(brew --prefix libssh2)"

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
                "  brew install libssh2 && "
                "cmake -B build -DCMAKE_PREFIX_PATH=\"$(brew --prefix libssh2)\"")
        endif()

        add_library(arterm_libssh2_imported UNKNOWN IMPORTED)
        set_target_properties(arterm_libssh2_imported PROPERTIES
            IMPORTED_LOCATION "${LIBSSH2_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LIBSSH2_INCLUDE_DIR}")
        add_library(arterm::libssh2 ALIAS arterm_libssh2_imported)
    endif()
endif()

find_package(Threads REQUIRED)
