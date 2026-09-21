find_package(Catch2 3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
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
        core/FileTests.cpp
        core/HashTests.cpp
        core/JobSystemTests.cpp
        core/LogTests.cpp
        core/MappedFileTests.cpp
        core/SlotMapTests.cpp
        core/UuidTests.cpp
    LIBRARIES
        Devex::Engine
)
target_compile_definitions(devex_core_tests PRIVATE DEVEX_TEST_EXPECTED_VERSION="${PROJECT_VERSION}")

devex_add_tests(math
    SOURCES
        math/ProjectionTests.cpp
        math/TransformTests.cpp
    LIBRARIES
        Devex::Engine
)

devex_add_tests(reflection
    SOURCES
        reflection/ReflectionTests.cpp
    LIBRARIES
        Devex::Engine
)

devex_add_tests(serialization
    SOURCES
        serialization/BinaryTests.cpp
        serialization/TextTests.cpp
    LIBRARIES
        Devex::Engine
)

devex_add_tests(scene
    SOURCES
        scene/DynamicComponentTests.cpp
        scene/EntityRefAndListTests.cpp
        scene/PrefabTests.cpp
        scene/SceneSerializerTests.cpp
        scene/SceneTests.cpp
    LIBRARIES
        Devex::Engine
)

devex_add_tests(animation
    SOURCES
        animation/AnimationTests.cpp
    LIBRARIES
        Devex::Engine
)
target_compile_definitions(devex_animation_tests
    PRIVATE DEVEX_TEST_DATA_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

devex_add_tests(asset
    SOURCES
        asset/ArtifactTests.cpp
        asset/AssetDatabaseTests.cpp
        asset/GltfImporterTests.cpp
        asset/MeshDataTests.cpp
        asset/MetaFileTests.cpp
        asset/PackageTests.cpp
        asset/TextureProcessingTests.cpp
    LIBRARIES
        Devex::Engine
)
target_compile_definitions(devex_asset_tests
    PRIVATE DEVEX_TEST_DATA_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

devex_add_tests(audio
    SOURCES
        audio/AudioTests.cpp
    LIBRARIES
        Devex::Engine
)
target_compile_definitions(devex_audio_tests
    PRIVATE DEVEX_TEST_DATA_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

devex_add_tests(platform
    SOURCES
        platform/InputTests.cpp
        platform/ProcessTests.cpp
    LIBRARIES
        Devex::Engine
)

devex_add_tests(physics
    SOURCES
        physics/PhysicsWorldTests.cpp
    LIBRARIES
        Devex::Engine
)

# Tests tagged [gpu] open a hidden window and need a Vulkan 1.4 GPU.
devex_add_tests(render
    SOURCES
        render/LightingTests.cpp
        render/PhotometryTests.cpp
        render/RendererTests.cpp
        render/SelectionTests.cpp
    LIBRARIES
        Devex::Engine
)
# Selection logic is internal to the module but free of Vulkan types.
target_include_directories(devex_render_tests PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")

# Command and log tests run anywhere; the overlay test is tagged [gpu].
devex_add_tests(tools
    SOURCES
        tools/CodeEditingTests.cpp
        tools/EditorMathTests.cpp
        tools/IconsTests.cpp
        tools/LogBufferTests.cpp
        tools/ProjectListTests.cpp
        tools/SceneCommandsTests.cpp
        tools/SceneTabsTests.cpp
        tools/TextDocumentTests.cpp
        tools/TextEditorTests.cpp
        tools/ThemeTests.cpp
        tools/ToolsOverlayTests.cpp
    LIBRARIES
        Devex::Engine
)
# The editor's internals are tested directly; their headers use ImGui's types.
target_include_directories(devex_tools_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/engine/src"
    $<TARGET_PROPERTY:imgui::imgui,INTERFACE_INCLUDE_DIRECTORIES>
)
target_compile_definitions(devex_tools_tests PRIVATE
    DEVEX_TEST_RESOURCES_DIRECTORY="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources"
)

# A game module loaded by the runtime tests.
add_library(devex_test_game_module SHARED runtime/TestGameModule.cpp)
target_link_libraries(devex_test_game_module PRIVATE Devex::Engine)
set_target_properties(devex_test_game_module PROPERTIES FOLDER "Tests")
devex_set_warnings(devex_test_game_module)

# An old game ABI must be rejected before registering any of its components.
add_library(devex_test_incompatible_game_module SHARED runtime/TestIncompatibleGameModule.cpp)
target_link_libraries(devex_test_incompatible_game_module PRIVATE Devex::Engine)
set_target_properties(devex_test_incompatible_game_module PROPERTIES FOLDER "Tests")
devex_set_warnings(devex_test_incompatible_game_module)

# The dummy SDL video driver lets the main loop run without a display.
devex_add_tests(runtime
    SOURCES
        runtime/ApplicationTests.cpp
        runtime/FixedTimestepTests.cpp
        runtime/GameCodeBuilderTests.cpp
        runtime/GameExportTests.cpp
        runtime/ManagedGameTests.cpp
        runtime/GameModuleTests.cpp
        runtime/SceneExtractionTests.cpp
    LIBRARIES
        Devex::Engine
    ENVIRONMENT
        SDL_VIDEO_DRIVER=dummy
)
add_dependencies(devex_runtime_tests devex_test_game_module devex_test_incompatible_game_module)

# A C# game assembly for the tests of the managed runtime, built when the .NET SDK is installed.
if(DEVEX_DOTNET)
    set(DEVEX_TEST_GAME_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/managed-tests")
    file(GLOB DEVEX_TEST_GAME_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/managed/TestGame/*.cs")
    add_custom_command(
        OUTPUT "${DEVEX_TEST_GAME_DIRECTORY}/TestGame.dll"
        COMMAND "${DEVEX_DOTNET}" build "${CMAKE_CURRENT_SOURCE_DIR}/managed/TestGame/TestGame.csproj"
                -c Release -o "${DEVEX_TEST_GAME_DIRECTORY}" --nologo -v quiet
                "-p:DevexManagedDirectory=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/managed"
                "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/managed-tests/obj/"
        DEPENDS ${DEVEX_TEST_GAME_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/managed/TestGame/TestGame.csproj" devex_managed
        COMMENT "Building the C# game of the tests"
        VERBATIM
    )
    add_custom_target(devex_test_managed_game DEPENDS "${DEVEX_TEST_GAME_DIRECTORY}/TestGame.dll")
    set_target_properties(devex_test_managed_game PROPERTIES FOLDER "Tests")
    add_dependencies(devex_runtime_tests devex_test_managed_game)
    target_compile_definitions(devex_runtime_tests PRIVATE DEVEX_TEST_MANAGED_GAME="${DEVEX_TEST_GAME_DIRECTORY}/TestGame.dll")
endif()
target_include_directories(devex_runtime_tests PRIVATE "${PROJECT_SOURCE_DIR}/engine/src")
target_compile_definitions(devex_runtime_tests
    PRIVATE DEVEX_TEST_GAME_MODULE="$<TARGET_FILE:devex_test_game_module>"
            DEVEX_TEST_INCOMPATIBLE_GAME_MODULE="$<TARGET_FILE:devex_test_incompatible_game_module>"
            DEVEX_TEST_DATA_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/data"
)
