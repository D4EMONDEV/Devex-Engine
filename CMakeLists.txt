cmake_minimum_required(VERSION 3.25)

option(DEVEX_BUILD_EDITOR "Build the Devex editor" ON)
option(DEVEX_BUILD_PLAYER "Build the Devex player, which runs projects outside the editor" ON)
option(DEVEX_BUILD_TESTS "Build Devex unit tests" ON)
option(DEVEX_BUILD_FUZZERS "Build the Clang libFuzzer targets for file readers" OFF)
option(DEVEX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors for Devex targets" OFF)
# The renderer of the browser. On a computer it serves the tests and development, beside Vulkan, and stays out of
# what ships: the web preset turns it on, a build of the tests may.
option(DEVEX_WEBGPU "Build the WebGPU renderer, with Dawn" OFF)
# The light renderer that will take WebGPU's place: OpenGL 3.3 on a computer, OpenGL ES 3.0 on a phone, WebGL2 in a
# browser. It draws with what the system has, and its shaders are translated at the build by spirv-cross, from vcpkg.
option(DEVEX_OPENGL "Build the OpenGL renderer" ON)

# Dependencies come from vcpkg in manifest mode: the toolchain must be known before project().
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DevexVcpkg)

project(DevexEngine
    VERSION 0.12.0
    DESCRIPTION "Open-source 3D game engine"
    LANGUAGES C CXX
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

# The C++ runtime that Release builds may redistribute, copied into bin/redist, where the export of a
# game takes it from so that players need nothing installed.
if(MSVC AND CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)
    include(InstallRequiredSystemLibraries)
    if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        file(COPY ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/redist")
    endif()
endif()
# Release builds keep their symbols in .pdb files beside the binaries, with the same optimized code:
# the report of a crash then names the functions and the lines it went through. The export of a
# game does not copy them.
if(MSVC)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo,Release>:ProgramDatabase>")
    add_link_options("$<$<CONFIG:Release>:/DEBUG;/OPT:REF;/OPT:ICF>")
endif()
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
# On Linux, the programs find the engine library in lib/ next to bin/ wherever the folder goes, in a
# build as in an installed engine.
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(DevexShowIncludes)
# C++ exceptions are off by default in Emscripten: Catch2, nlohmann::json and the standard library throw some.
if(EMSCRIPTEN)
    add_compile_options(-fwasm-exceptions)
    add_link_options(-fwasm-exceptions)
endif()
# The web with threads (preset web-threads): everything compiled with -pthread, and the workers made before the program
# starts, since a page makes them only once it has its hand back, which a thread waiting for them never gives.
option(DEVEX_WEB_THREADS "Build the web with threads: pthreads, which need the headers COOP and COEP" OFF)
set(DEVEX_WEB_THREAD_POOL "-sPTHREAD_POOL_SIZE=Math.min(navigator.hardwareConcurrency,8)")
if(EMSCRIPTEN AND DEVEX_WEB_THREADS)
    add_compile_options(-pthread)
    # Memory that grows is slower to read from JavaScript with threads, which the engine does little: no warning for it.
    add_link_options(-pthread "${DEVEX_WEB_THREAD_POOL}" -Wno-pthreads-mem-growth)
endif()

include(DevexShaders)
include(DevexWarnings)
include(DevexBranding)
include(DevexLicenses)

add_subdirectory(shaders)
add_subdirectory(engine)
if(DEVEX_BUILD_FUZZERS)
    add_subdirectory(tests/fuzz)
endif()
# devex-bindgen runs during the build, on the build machine: not built for a phone or a browser.
if(NOT ANDROID AND NOT EMSCRIPTEN)
    add_subdirectory(tools/bindgen)
endif()
if(DEVEX_DOTNET)
    add_subdirectory(managed)
endif()

if(DEVEX_BUILD_EDITOR)
    add_subdirectory(apps/editor)
    if(TARGET devex_managed)
        add_dependencies(devex_editor devex_managed)
    endif()
endif()

if(DEVEX_BUILD_PLAYER)
    add_subdirectory(apps/player)
    if(TARGET devex_managed)
        add_dependencies(devex_player devex_managed)
    endif()
endif()

include(DevexInstall)

# The default editor Play command supervises the standalone player beside it.
if(TARGET devex_editor AND TARGET devex_player)
    add_dependencies(devex_editor devex_player)
endif()

if(DEVEX_BUILD_TESTS)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
