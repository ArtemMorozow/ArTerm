# Global build configuration for ArTerm.
#
# ArTerm targets macOS only, built with clang against C++23 and the system
# frameworks.

option(ARTERM_BUILD_TESTS "Build the ArTerm unit tests" ON)
option(ARTERM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(ARTERM_ENABLE_ASAN "Build with AddressSanitizer + UBSan" OFF)

# C++23 is a hard requirement: std::expected carries every fallible operation in
# the codebase and there is no fallback path for a toolchain without it.
set(ARTERM_CXX_STANDARD 23)

if(NOT "cxx_std_23" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    message(FATAL_ERROR
        "ArTerm requires a C++23 toolchain; ${CMAKE_CXX_COMPILER_ID} "
        "${CMAKE_CXX_COMPILER_VERSION} does not offer one.")
endif()

set(CMAKE_CXX_STANDARD ${ARTERM_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

if(NOT APPLE)
    message(FATAL_ERROR "ArTerm is a macOS-only application.")
endif()

if(APPLE)
    # Universal binary by default: Apple Silicon first, Intel for compatibility.
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    endif()
    # 13.3 rather than 13.0: libc++'s std::format instantiates the floating-point
    # formatter whatever the format string says, and std::to_chars(long double)
    # is unavailable before 13.3. Ventura shipped 13.3 in March 2023.
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "" FORCE)
    endif()
endif()

# ---------------------------------------------------------------------------
# arterm::flags - interface target carrying the shared compile settings.
# ---------------------------------------------------------------------------
add_library(arterm_flags INTERFACE)
add_library(arterm::flags ALIAS arterm_flags)

target_compile_features(arterm_flags INTERFACE cxx_std_${ARTERM_CXX_STANDARD})

if(MSVC)
    target_compile_options(arterm_flags INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(arterm_flags INTERFACE
        -Wall -Wextra -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wcast-align
        -Wunused
        -Wno-missing-field-initializers)
endif()

if(ARTERM_WARNINGS_AS_ERRORS AND NOT MSVC)
    target_compile_options(arterm_flags INTERFACE -Werror)
endif()

if(ARTERM_ENABLE_ASAN AND NOT MSVC)
    target_compile_options(arterm_flags INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(arterm_flags INTERFACE -fsanitize=address,undefined)
endif()

target_compile_definitions(arterm_flags INTERFACE
    ARTERM_VERSION="${PROJECT_VERSION}")
