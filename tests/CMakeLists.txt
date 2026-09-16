find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

# devex_add_tests(<module> SOURCES <files...> LIBRARIES <targets...> [ENVIRONMENT <VAR=value...>])
# Builds devex_<module>_tests and registers each Catch2 test case with CTest.
function(devex_add_tests module)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;LIBRARIES;ENVIRONMENT")
    set(target devex_${module}_tests)

    add_executable(${target} ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE ${ARG_LIBRARIES} Catch2::Catch2WithMain)
    set_target_properties(${target} PROPERTIES FOLDER "Tests")
    devex_set_warnings(${target})

    set(discovery_properties)
    if(ARG_ENVIRONMENT)
        set(discovery_properties PROPERTIES ENVIRONMENT "${ARG_ENVIRONMENT}")
    endif()
    catch_discover_tests(${target}
        TEST_PREFIX "devex.${module}."
        DISCOVERY_MODE PRE_TEST
        ${discovery_properties}
    )
endfunction()

devex_add_tests(core
    SOURCES
        core/AssertTests.cpp
        core/BuildInfoTests.cpp
        core/ErrorTests.cpp
        core/LogTests.cpp
        core/SlotMapTests.cpp
    LIBRARIES
        Devex::Core
)
target_compile_definitions(devex_core_tests PRIVATE DEVEX_TEST_EXPECTED_VERSION="${PROJECT_VERSION}")

devex_add_tests(math
    SOURCES
        math/ProjectionTests.cpp
    LIBRARIES
        Devex::Math
)

devex_add_tests(asset
    SOURCES
        asset/GltfImporterTests.cpp
        asset/MeshDataTests.cpp
    LIBRARIES
        Devex::AssetImport
)
target_compile_definitions(devex_asset_tests
    PRIVATE DEVEX_TEST_DATA_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

devex_add_tests(platform
    SOURCES
        platform/InputTests.cpp
    LIBRARIES
        Devex::Platform
)

# Tests tagged [gpu] open a hidden window and need a Vulkan 1.4 GPU.
devex_add_tests(render
    SOURCES
        render/RendererTests.cpp
        render/SelectionTests.cpp
    LIBRARIES
        Devex::Render
)
# Selection logic is internal to the module but free of Vulkan types.
target_include_directories(devex_render_tests PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")

# The dummy SDL video driver lets the main loop run without a display.
devex_add_tests(runtime
    SOURCES
        runtime/ApplicationTests.cpp
        runtime/FixedTimestepTests.cpp
    LIBRARIES
        Devex::Runtime
    ENVIRONMENT
        SDL_VIDEO_DRIVER=dummy
)
