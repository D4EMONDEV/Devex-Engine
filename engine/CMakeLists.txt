find_package(basisu CONFIG REQUIRED)
find_package(efsw CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)
find_package(mikktspace CONFIG REQUIRED)
find_package(plutosvg CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(volk CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_path(DEVEX_MINIAUDIO_INCLUDE_DIR miniaudio.h REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)

# Every module is an object library, compiled into the single devex-engine shared library at the
# end of this file. Programs, tests and game modules link Devex::Engine, never a module directly,
# so that they all share one engine state: registries, logging, the renderer.

# Core: vocabulary types, errors, logging and assertions. Depends only on the standard library.
add_library(devex_core OBJECT)
add_library(Devex::Core ALIAS devex_core)

target_sources(devex_core
    PRIVATE
        src/core/Assert.cpp
        src/core/BuildInfo.cpp
        src/core/Error.cpp
        src/core/File.cpp
        src/core/Hash.cpp
        src/core/JobSystem.cpp
        src/core/Log.cpp
        src/core/MappedFile.cpp
        src/core/Uuid.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/devex/core/Assert.hpp
            include/devex/core/BuildInfo.hpp
            include/devex/core/Error.hpp
            include/devex/core/File.hpp
            include/devex/core/Hash.hpp
            include/devex/core/JobSystem.hpp
            include/devex/core/Log.hpp
            include/devex/core/MappedFile.hpp
            include/devex/core/Path.hpp
            include/devex/core/SlotMap.hpp
            include/devex/core/Time.hpp
            include/devex/core/Uuid.hpp
)

target_compile_features(devex_core PUBLIC cxx_std_23)
# Assertions are part of the public headers, so every consumer must agree on this definition.
target_compile_definitions(devex_core
    PUBLIC
        $<$<NOT:$<CONFIG:Release,MinSizeRel>>:DEVEX_ENABLE_ASSERTS>
)
set_source_files_properties(src/core/BuildInfo.cpp
    PROPERTIES COMPILE_DEFINITIONS "DEVEX_VERSION_STRING=\"${PROJECT_VERSION}\";DEVEX_BUILD_TYPE=\"$<CONFIG>\""
)
set_target_properties(devex_core PROPERTIES
    EXPORT_NAME Core
    FOLDER "Engine"
)
devex_set_warnings(devex_core)

# Math: GLM behind devex::math aliases, configured for Vulkan conventions.
add_library(devex_math INTERFACE)
add_library(Devex::Math ALIAS devex_math)

target_sources(devex_math
    INTERFACE
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/devex/math/Math.hpp
)

target_link_libraries(devex_math INTERFACE Devex::Core glm::glm-header-only)
target_compile_definitions(devex_math INTERFACE GLM_FORCE_DEPTH_ZERO_TO_ONE)
set_target_properties(devex_math PROPERTIES
    EXPORT_NAME Math
    FOLDER "Engine"
)

# Platform: windows, events and input on top of SDL3, which never appears in public headers.
add_library(devex_platform OBJECT)
add_library(Devex::Platform ALIAS devex_platform)

target_sources(devex_platform
    PRIVATE
        src/platform/Executable.cpp
        src/platform/Input.cpp
        src/platform/Platform.cpp
        src/platform/Process.cpp
        src/platform/SdlWindow.hpp
        src/platform/SharedLibrary.cpp
        src/platform/Window.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/devex/platform/Event.hpp
            include/devex/platform/Executable.hpp
            include/devex/platform/Input.hpp
            include/devex/platform/Key.hpp
            include/devex/platform/Platform.hpp
            include/devex/platform/Process.hpp
            include/devex/platform/SharedLibrary.hpp
            include/devex/platform/Window.hpp
)

target_link_libraries(devex_platform
    PUBLIC
        Devex::Core
        Devex::Math
    PRIVATE
        imgui::imgui
        SDL3::SDL3
        $<$<PLATFORM_ID:Windows>:dwmapi>
)
set_target_properties(devex_platform PROPERTIES
    EXPORT_NAME Platform
    FOLDER "Engine"
)
devex_set_warnings(devex_platform)

# Reflection: field descriptions of engine and game types, registered explicitly.
add_library(devex_reflection OBJECT)
add_library(Devex::Reflection ALIAS devex_reflection)

target_sources(devex_reflection
    PRIVATE
        src/reflection/Reflection.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/devex/reflection/Reflection.hpp
)

target_link_libraries(devex_reflection
    PUBLIC
        Devex::Core
        Devex::Math
)
set_target_properties(devex_reflection PROPERTIES
    EXPORT_NAME Reflection
    FOLDER "Engine"
)
devex_set_warnings(devex_reflection)

# Serialization: the readable text format shared by .dvx* files.
add_library(devex_serialization OBJECT)
add_library(Devex::Serialization ALIAS devex_serialization)

target_sources(devex_serialization
    PRIVATE
        src/serialization/Text.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/devex/serialization/Binary.hpp
            include/devex/serialization/Text.hpp
)

target_link_libraries(devex_serialization PUBLIC Devex::Core)
set_target_properties(devex_serialization PROPERTIES
    EXPORT_NAME Serialization
    FOLDER "Engine"
)
devex_set_warnings(devex_serialization)

# Asset: CPU-side asset data and cooked asset files, shared by the runtime and the tools.
add_library(devex_asset OBJECT)
add_library(Devex::Asset ALIAS devex_asset)

target_sources(devex_asset
    PRIVATE
        src/asset/AnimationData.cpp
        src/asset/Artifact.cpp
        src/asset/AssetType.cpp
        src/asset/FontData.cpp
        src/asset/MaterialData.cpp
        src/asset/MeshData.cpp
        src/asset/ModelData.cpp
        src/asset/Package.cpp
        src/asset/Primitives.cpp
        src/asset/Project.cpp
        src/asset/TextureData.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/devex/asset/AnimationData.hpp
            include/devex/asset/Artifact.hpp
            include/devex/asset/AssetId.hpp
            include/devex/asset/AssetSource.hpp
            include/devex/asset/AssetType.hpp
            include/devex/asset/AudioClipData.hpp
            include/devex/asset/FontData.hpp
            include/devex/asset/MaterialData.hpp
            include/devex/asset/MeshData.hpp
            include/devex/asset/ModelData.hpp
            include/devex/asset/Package.hpp
            include/devex/asset/Primitives.hpp
            include/devex/asset/Project.hpp
            include/devex/asset/TextureData.hpp
)

target_link_libraries(devex_asset
    PUBLIC
        Devex::Core
        Devex::Math
        Devex::Reflection
    PRIVATE
        Devex::Serialization
        mikktspace::mikktspace
        zstd::libzstd
)
set_target_properties(devex_asset PROPERTIES
    EXPORT_NAME Asset
    FOLDER "Engine"
)
devex_set_warnings(devex_asset)

# AssetImport: the asset database and the importers of source formats. Meant for tools, not
# shipped games.
add_library(devex_asset_import OBJECT)
add_library(Devex::AssetImport ALIAS devex_asset_import)

target_sources(devex_asset_import
    PRIVATE
        src/asset/import/AssetDatabase.cpp
        src/asset/import/AudioImporter.cpp
        src/asset/import/FontImporter.cpp
        src/asset/import/GltfImporter.cpp
        src/asset/import/Importer.cpp
        src/asset/import/MaterialFile.cpp
        src/asset/import/MetaFile.cpp
        src/asset/import/StbImage.cpp
        src/asset/import/TextureProcessing.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/devex/asset/import/AssetDatabase.hpp
            include/devex/asset/import/Importer.hpp
            include/devex/asset/import/MaterialFile.hpp
            include/devex/asset/import/MetaFile.hpp
            include/devex/asset/import/TextureProcessing.hpp
)

target_include_directories(devex_asset_import SYSTEM PRIVATE ${Stb_INCLUDE_DIR})
target_link_libraries(devex_asset_import
    PUBLIC
        Devex::Asset
        Devex::Core
        Devex::Serialization
    PRIVATE
        Devex::Audio
        Devex::Scene
        basisu::basisu_encoder
        efsw::efsw
        fastgltf::fastgltf
)
set_target_properties(devex_asset_import PROPERTIES
    EXPORT_NAME AssetImport
    FOLDER "Engine"
)
devex_set_warnings(devex_asset_import)

# Dear ImGui's Vulkan backend, vendored from the ImGui version pinned by vcpkg and compiled with
# volk: the vcpkg build links vulkan-1 directly, whose symbols clash with volk's function pointers.
add_library(devex_imgui_vulkan STATIC)
target_sources(devex_imgui_vulkan
    PRIVATE
        "${PROJECT_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_vulkan.cpp"
)
target_include_directories(devex_imgui_vulkan
    SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/third_party/imgui/backends"
)
target_compile_definitions(devex_imgui_vulkan PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
target_link_libraries(devex_imgui_vulkan
    PUBLIC
        imgui::imgui
        volk::volk_headers
)
set_target_properties(devex_imgui_vulkan PROPERTIES FOLDER "ThirdParty")

# Render: Renderer facade over Vulkan, whose types stay inside src/render/vulkan.
add_library(devex_render OBJECT)
add_library(Devex::Render ALIAS devex_render)

target_sources(devex_render
    PRIVATE
        src/render/Culling.cpp
        src/render/Lighting.cpp
        src/render/Lighting.hpp
        src/render/Photometry.cpp
        src/render/Renderer.cpp
        src/render/Selection.cpp
        src/render/Selection.hpp
        src/render/vulkan/Commands.cpp
        src/render/vulkan/Commands.hpp
        src/render/vulkan/DescriptorSets.cpp
        src/render/vulkan/DescriptorSets.hpp
        src/render/vulkan/Device.cpp
        src/render/vulkan/Device.hpp
        src/render/vulkan/EnvironmentBaker.cpp
        src/render/vulkan/EnvironmentBaker.hpp
        src/render/vulkan/GpuData.hpp
        src/render/vulkan/Instance.cpp
        src/render/vulkan/Instance.hpp
        src/render/vulkan/Memory.cpp
        src/render/vulkan/Memory.hpp
        src/render/vulkan/Pipeline.cpp
        src/render/vulkan/Pipeline.hpp
        src/render/vulkan/RenderGraph.cpp
        src/render/vulkan/RenderGraph.hpp
        src/render/vulkan/Swapchain.cpp
        src/render/vulkan/Swapchain.hpp
        src/render/vulkan/Upload.cpp
        src/render/vulkan/Upload.hpp
        src/render/vulkan/Vulkan.cpp
        src/render/vulkan/Vulkan.hpp
        src/render/vulkan/VulkanRenderer.cpp
        src/render/vulkan/VulkanRenderer.hpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/devex/render/Culling.hpp
            include/devex/render/Gpu.hpp
            include/devex/render/Photometry.hpp
            include/devex/render/Renderer.hpp
            include/devex/render/RenderWorld.hpp
)

target_link_libraries(devex_render
    PUBLIC
        Devex::Asset
        Devex::Core
        Devex::Math
        Devex::Platform
    PRIVATE
        devex_imgui_vulkan
        volk::volk
        volk::volk_headers
        GPUOpen::VulkanMemoryAllocator
)
# VMA receives every Vulkan function from volk instead of linking or loading them itself.
target_compile_definitions(devex_render
    PRIVATE
        VMA_STATIC_VULKAN_FUNCTIONS=0
        VMA_DYNAMIC_VULKAN_FUNCTIONS=0
)
set_target_properties(devex_render PROPERTIES
    EXPORT_NAME Render
    FOLDER "Engine"
)
devex_set_warnings(devex_render)
# The renderer loads the compiled shaders at runtime.
add_dependencies(devex_render devex_shaders)

# Scene: entities, components in sparse sets, hierarchy, .dvxscene files and prefabs.
add_library(devex_scene OBJECT)
add_library(Devex::Scene ALIAS devex_scene)

target_sources(devex_scene
    PRIVATE
        src/scene/ComponentRegistry.cpp
        src/scene/Components.cpp
        src/scene/DynamicComponent.cpp
        src/scene/FieldValue.cpp
        src/scene/ModelInstantiation.cpp
        src/scene/PhysicsComponents.cpp
        src/scene/UiComponents.cpp
        src/scene/AnimationComponents.cpp
        src/scene/AudioComponents.cpp
        src/scene/Prefab.cpp
        src/scene/Scene.cpp
        src/scene/SceneSerializer.cpp
        src/scene/SceneText.hpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/devex/scene/ComponentPool.hpp
            include/devex/scene/ComponentRegistry.hpp
            include/devex/scene/Components.hpp
            include/devex/scene/DynamicComponent.hpp
            include/devex/scene/Entity.hpp
            include/devex/scene/FieldValue.hpp
            include/devex/scene/ModelInstantiation.hpp
            include/devex/scene/PhysicsComponents.hpp
            include/devex/scene/UiComponents.hpp
            include/devex/scene/AnimationComponents.hpp
            include/devex/scene/AudioComponents.hpp
            include/devex/scene/Prefab.hpp
            include/devex/scene/Scene.hpp
            include/devex/scene/SceneSerializer.hpp
            include/devex/scene/View.hpp
)

target_link_libraries(devex_scene
    PUBLIC
        Devex::Asset
        Devex::Core
        Devex::Math
        Devex::Reflection
        Devex::Serialization
)
set_target_properties(devex_scene PROPERTIES
    EXPORT_NAME Scene
    FOLDER "Engine"
)
devex_set_warnings(devex_scene)

# Physics: rigid bodies, colliders and characters simulated with Jolt Physics from scene components.
add_library(devex_physics OBJECT)
add_library(Devex::Physics ALIAS devex_physics)

target_sources(devex_physics
    PRIVATE
        src/physics/PhysicsWorld.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/devex/physics/PhysicsWorld.hpp
)

target_link_libraries(devex_physics
    PUBLIC
        Devex::Asset
        Devex::Core
        Devex::Math
        Devex::Scene
    PRIVATE
        Jolt::Jolt
)
set_target_properties(devex_physics PROPERTIES
    EXPORT_NAME Physics
    FOLDER "Engine"
)
devex_set_warnings(devex_physics)

# Animation: animation clips, their sampling and blending, and the skeletons of the Animator
# components of a game.
add_library(devex_animation OBJECT)
add_library(Devex::Animation ALIAS devex_animation)

target_sources(devex_animation
    PRIVATE
        src/animation/AnimationWorld.cpp
        src/animation/Clip.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/devex/animation/AnimationWorld.hpp
            include/devex/animation/Clip.hpp
)

target_link_libraries(devex_animation
    PUBLIC
        Devex::Asset
        Devex::Core
        Devex::Math
        Devex::Scene
)
set_target_properties(devex_animation PROPERTIES
    EXPORT_NAME Animation
    FOLDER "Engine"
)
devex_set_warnings(devex_animation)

# Audio: clips, the mixer and its groups, and the sounds of the AudioSource components of a game,
# played with miniaudio (Ogg Vorbis decoded by stb_vorbis).
add_library(devex_audio OBJECT)
add_library(Devex::Audio ALIAS devex_audio)

target_sources(devex_audio
    PRIVATE
        src/audio/AudioEngine.cpp
        src/audio/AudioWorld.cpp
        src/audio/Clip.cpp
        src/audio/Miniaudio.cpp
        src/audio/Voice.hpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/devex/audio/AudioEngine.hpp
            include/devex/audio/AudioWorld.hpp
            include/devex/audio/Clip.hpp
)

target_include_directories(devex_audio SYSTEM PRIVATE ${DEVEX_MINIAUDIO_INCLUDE_DIR} ${Stb_INCLUDE_DIR})
target_link_libraries(devex_audio
    PUBLIC
        Devex::Asset
        Devex::Core
        Devex::Math
        Devex::Scene
)
set_target_properties(devex_audio PROPERTIES
    EXPORT_NAME Audio
    FOLDER "Engine"
)
devex_set_warnings(devex_audio)

# Interface: the placement of the Canvas and UiRect components on the screen, the buttons they
# answer with, and the quads and letters they are drawn from.
add_library(devex_ui OBJECT)
add_library(Devex::Ui ALIAS devex_ui)

target_sources(devex_ui
    PRIVATE
        src/ui/DrawList.cpp
        src/ui/Layout.cpp
        src/ui/TextLayout.cpp
        src/ui/UiWorld.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/devex/ui/DrawList.hpp
            include/devex/ui/Layout.hpp
            include/devex/ui/TextLayout.hpp
            include/devex/ui/UiWorld.hpp
)

target_link_libraries(devex_ui
    PUBLIC
        Devex::Asset
        Devex::Core
        Devex::Math
        Devex::Render
        Devex::Scene
)
set_target_properties(devex_ui PROPERTIES
    EXPORT_NAME Ui
    FOLDER "Engine"
)
devex_set_warnings(devex_ui)

# The C# side of the engine is built with the .NET SDK when it is installed (see managed/). Without
# it, the engine runs games written in C++ only.
find_program(DEVEX_DOTNET NAMES dotnet)
if(NOT DEVEX_DOTNET)
    message(STATUS "The .NET SDK was not found: games of this build can only be written in C++")
endif()

# Resources of the tools: Noto Sans and JetBrains Mono (SIL Open Font License), Lucide icons (ISC)
# and the Devex logo, with their licenses.
file(GLOB DEVEX_FONT_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/third_party/fonts/*.ttf")
file(GLOB DEVEX_ICON_FILES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/third_party/lucide/icons/*.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/*.svg"
)
set(DEVEX_RESOURCES_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources")
set(DEVEX_RESOURCES_STAMP "${CMAKE_CURRENT_BINARY_DIR}/resources.stamp")
add_custom_command(
    OUTPUT "${DEVEX_RESOURCES_STAMP}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DEVEX_RESOURCES_DIRECTORY}/fonts" "${DEVEX_RESOURCES_DIRECTORY}/icons"
            "${DEVEX_RESOURCES_DIRECTORY}/licenses"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${DEVEX_FONT_FILES} "${DEVEX_RESOURCES_DIRECTORY}/fonts"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${DEVEX_ICON_FILES} "${DEVEX_RESOURCES_DIRECTORY}/icons"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${PROJECT_SOURCE_DIR}/third_party/fonts/NotoSans-OFL.txt"
            "${PROJECT_SOURCE_DIR}/third_party/fonts/JetBrainsMono-OFL.txt"
            "${DEVEX_RESOURCES_DIRECTORY}/licenses"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PROJECT_SOURCE_DIR}/third_party/lucide/LICENSE"
            "${DEVEX_RESOURCES_DIRECTORY}/licenses/Lucide-LICENSE.txt"
    COMMAND "${CMAKE_COMMAND}" -E touch "${DEVEX_RESOURCES_STAMP}"
    DEPENDS ${DEVEX_FONT_FILES} ${DEVEX_ICON_FILES}
    COMMENT "Copying the fonts and icons of the tools"
    VERBATIM
)
add_custom_target(devex_resources DEPENDS "${DEVEX_RESOURCES_STAMP}")
set_target_properties(devex_resources PROPERTIES FOLDER "Engine")

# Tools: docked ImGui panels over the game, with undoable scene editing.
add_library(devex_tools OBJECT)
add_library(Devex::Tools ALIAS devex_tools)

target_sources(devex_tools
    PRIVATE
        src/tools/AnimationPanel.cpp
        src/tools/AssetsPanel.cpp
        src/tools/AudioInspector.cpp
        src/tools/CommandHistory.cpp
        src/tools/CodeArea.cpp
        src/tools/CodeArea.hpp
        src/tools/CodeCompletion.cpp
        src/tools/CodeCompletion.hpp
        src/tools/CodeHighlight.cpp
        src/tools/CodeHighlight.hpp
        src/tools/CodeOutline.cpp
        src/tools/CodeOutline.hpp
        src/tools/CodePanel.cpp
        src/tools/TextDocument.cpp
        src/tools/TextDocument.hpp
        src/tools/TextEditorPanel.cpp
        src/tools/ConsolePanel.cpp
        src/tools/EditorCamera.cpp
        src/tools/EditorCamera.hpp
        src/tools/EditorMenus.cpp
        src/tools/EditorOverlay.cpp
        src/tools/EditorSession.cpp
        src/tools/EditorView.cpp
        src/tools/EditorView.hpp
        src/tools/ExportWindow.cpp
        src/tools/Gizmo.cpp
        src/tools/Gizmo.hpp
        src/tools/HierarchyPanel.cpp
        src/tools/Icons.cpp
        src/tools/Icons.hpp
        src/tools/InspectorPanel.cpp
        src/tools/InterfacePanel.cpp
        src/tools/LogBuffer.cpp
        src/tools/ProjectList.cpp
        src/tools/ProjectList.hpp
        src/tools/ProjectManager.cpp
        src/tools/SceneCommands.cpp
        src/tools/SceneTabs.cpp
        src/tools/SceneTabs.hpp
        src/tools/StatisticsPanel.cpp
        src/tools/Theme.cpp
        src/tools/Theme.hpp
        src/tools/ToolsOverlay.cpp
        src/tools/ToolsState.hpp
        src/tools/ViewportPanel.cpp
        src/tools/Widgets.cpp
        src/tools/Widgets.hpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/devex/tools/CommandHistory.hpp
            include/devex/tools/LogBuffer.hpp
            include/devex/tools/SceneCommands.hpp
            include/devex/tools/ToolsOverlay.hpp
)

target_link_libraries(devex_tools
    PUBLIC
        Devex::Ui
        Devex::Animation
        Devex::AssetImport
        Devex::Audio
        Devex::Core
        Devex::Platform
        Devex::Render
        Devex::Scene
        Devex::Serialization
    PRIVATE
        imgui::imgui
        plutosvg::plutosvg
)
target_compile_definitions(devex_tools PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
# The fonts and icons of the tools, loaded at runtime from resources/ next to the executables.
add_dependencies(devex_tools devex_resources)
set_target_properties(devex_tools PROPERTIES
    EXPORT_NAME Tools
    FOLDER "Engine"
)
devex_set_warnings(devex_tools)

# Runtime: application lifecycle, the main loop and the scene to renderer bridge.
add_library(devex_runtime OBJECT)
add_library(Devex::Runtime ALIAS devex_runtime)

target_sources(devex_runtime
    PRIVATE
        src/runtime/Application.cpp
        src/runtime/AssetManager.cpp
        src/runtime/FixedTimestep.cpp
        src/runtime/Game.cpp
        src/runtime/GameCodeBuilder.cpp
        src/runtime/GameCodeBuilder.hpp
        src/runtime/GameExport.cpp
        src/runtime/GameModule.cpp
        src/runtime/ComponentViews.cpp
        src/runtime/ManagedCodeBuilder.cpp
        src/runtime/ManagedCodeBuilder.hpp
        src/runtime/ManagedGame.cpp
        src/runtime/ManagedGame.hpp
        src/runtime/SceneExtraction.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/devex/runtime/Application.hpp
            include/devex/runtime/AssetManager.hpp
            include/devex/runtime/FixedTimestep.hpp
            include/devex/runtime/Game.hpp
            include/devex/runtime/GameExport.hpp
            include/devex/runtime/GameModule.hpp
            include/devex/runtime/SceneExtraction.hpp
)

target_link_libraries(devex_runtime
    PUBLIC
        Devex::Animation
        Devex::AssetImport
        Devex::Audio
        Devex::Core
        Devex::Physics
        Devex::Platform
        Devex::Render
        Devex::Scene
    PRIVATE
        Devex::Tools
)
set_target_properties(devex_runtime PROPERTIES
    EXPORT_NAME Runtime
    FOLDER "Engine"
)
devex_set_warnings(devex_runtime)

# Engine: every module in one shared library. Its symbols are all exported, so that game modules
# use the same C++ API as the editor. They must be built with the same compiler and options.
add_library(devex_engine SHARED src/EngineLibrary.cpp)
add_library(Devex::Engine ALIAS devex_engine)
target_link_libraries(devex_engine
    PUBLIC
        devex_animation
        devex_asset
        devex_asset_import
        devex_audio
        devex_core
        devex_math
        devex_physics
        devex_platform
        devex_reflection
        devex_render
        devex_runtime
        devex_scene
        devex_serialization
        devex_tools
        devex_ui
)
set_target_properties(devex_engine PROPERTIES
    OUTPUT_NAME "devex-engine"
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    EXPORT_NAME Engine
    FOLDER "Engine"
)
devex_set_warnings(devex_engine)

# The package that game modules find: cmake/DevexConfig.cmake in the build folder, next to bin/.
configure_file("${PROJECT_SOURCE_DIR}/cmake/DevexGameConfig.cmake.in"
    "${CMAKE_BINARY_DIR}/cmake/DevexConfig.cmake.in" @ONLY)
file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/cmake/DevexConfig.cmake"
    INPUT "${CMAKE_BINARY_DIR}/cmake/DevexConfig.cmake.in"
)
