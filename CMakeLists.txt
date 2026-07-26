cmake_minimum_required(VERSION 3.25)

project(ArTerm
    VERSION 0.1.0
    DESCRIPTION "ArTerm - SSH client and SFTP/SCP file manager for macOS"
    HOMEPAGE_URL "https://github.com/ArtemMorozow/ArTerm"
    LANGUAGES CXX OBJCXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(ArTermOptions)
include(ArTermDependencies)

add_subdirectory(src)

if(ARTERM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(ArTermPackaging)
