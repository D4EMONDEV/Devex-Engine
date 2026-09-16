cmake_minimum_required(VERSION 3.25)

option(DEVEX_BUILD_SANDBOX "Build the executable used to exercise engine systems" ON)
option(DEVEX_BUILD_TESTS "Build Devex unit tests" ON)
option(DEVEX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors for Devex targets" OFF)

# Dependencies come from vcpkg in manifest mode: the toolchain must be known before project().
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DevexVcpkg)

project(DevexEngine
    VERSION 0.1.0
    DESCRIPTION "Open-source 3D game engine"
    LANGUAGES CXX
)

# C++23 is the current stable baseline. C++26 is available only for experiments.
set(DEVEX_CXX_STANDARD "23" CACHE STRING "C++ language standard (23 or 26)")
set_property(CACHE DEVEX_CXX_STANDARD PROPERTY STRINGS 23 26)
set(_devex_supported_cxx_standards 23 26)
list(FIND _devex_supported_cxx_standards "${DEVEX_CXX_STANDARD}" _devex_cxx_standard_index)
if(_devex_cxx_standard_index EQUAL -1)
    message(FATAL_ERROR "DEVEX_CXX_STANDARD must be 23 or 26, not '${DEVEX_CXX_STANDARD}'.")
endif()

set(CMAKE_CXX_STANDARD ${DEVEX_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(DevexWarnings)

add_subdirectory(engine)

if(DEVEX_BUILD_SANDBOX)
    add_subdirectory(apps/sandbox)
endif()

if(DEVEX_BUILD_TESTS)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
