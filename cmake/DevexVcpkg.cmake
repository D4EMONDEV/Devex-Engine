# Selects the vcpkg toolchain and the manifest features. Must be included before project().

if(DEVEX_BUILD_TESTS)
    list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

if(DEFINED CMAKE_TOOLCHAIN_FILE)
    return()
endif()

set(_devex_vcpkg_root "")
if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(_devex_vcpkg_root "$ENV{VCPKG_ROOT}")
else()
    find_program(DEVEX_VCPKG_EXECUTABLE vcpkg)
    if(DEVEX_VCPKG_EXECUTABLE)
        cmake_path(GET DEVEX_VCPKG_EXECUTABLE PARENT_PATH _devex_vcpkg_candidate)
        if(EXISTS "${_devex_vcpkg_candidate}/scripts/buildsystems/vcpkg.cmake")
            set(_devex_vcpkg_root "${_devex_vcpkg_candidate}")
        endif()
    endif()
endif()

if(NOT _devex_vcpkg_root)
    message(FATAL_ERROR
        "vcpkg was not found. Set the VCPKG_ROOT environment variable to your vcpkg directory, "
        "or pass -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake."
    )
endif()

set(CMAKE_TOOLCHAIN_FILE "${_devex_vcpkg_root}/scripts/buildsystems/vcpkg.cmake"
    CACHE FILEPATH "CMake toolchain file (vcpkg)"
)
message(STATUS "Devex: using vcpkg from ${_devex_vcpkg_root}")
