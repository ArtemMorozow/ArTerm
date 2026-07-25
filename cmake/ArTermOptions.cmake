# Global build configuration for ArTerm.
#
# The project targets C++26 built with clang. GCC is tolerated for CI/linux
# smoke builds but macOS (the shipping platform) is always clang.

option(ARTERM_BUILD_TESTS "Build the ArTerm unit tests" ON)
option(ARTERM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(ARTERM_ENABLE_ASAN "Build with AddressSanitizer + UBSan" OFF)

# The shipping build is clang + libc++ + C++26. Older toolchains (notably the
# GCC/libstdc++ combination used for Linux CI smoke builds) fall back to C++23,
# which is enough for everything the code actually uses - std::expected being
# the only library feature that matters here.
include(CheckCXXCompilerFlag)

if("cxx_std_26" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    set(ARTERM_CXX_STANDARD 26)
else()
    set(ARTERM_CXX_STANDARD 23)
    message(STATUS "C++26 is unavailable with ${CMAKE_CXX_COMPILER_ID} "
                   "${CMAKE_CXX_COMPILER_VERSION}; falling back to C++23")
endif()

set(CMAKE_CXX_STANDARD ${ARTERM_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

if(APPLE)
    # Universal binary by default: Apple Silicon first, Intel for compatibility.
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "" FORCE)
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
    ARTERM_VERSION="${PROJECT_VERSION}"
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QT_USE_QSTRINGBUILDER
    $<$<CONFIG:Release>:QT_NO_DEBUG_OUTPUT>)
