find_package(fastgltf CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(volk CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

# Core: vocabulary types, errors, logging and assertions. Depends only on the standard library.
add_library(devex_core STATIC)
add_library(Devex::Core ALIAS devex_core)

target_sources(devex_core
    PRIVATE
        src/core/Assert.cpp
        src/core/BuildInfo.cpp
        src/core/Error.cpp
        src/core/File.cpp
        src/core/Log.cpp
        src/core/Uuid.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/devex/core/Assert.hpp
            include/devex/core/BuildInfo.hpp
            include/devex/core/Error.hpp
            include/devex/core/File.hpp
            include/devex/core/Log.hpp
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
    PROPERTIES COMPILE_DEFINITIONS "DEVEX_VERSION_STRING=\"${PROJECT_VERSION}\""
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
add_library(devex_platform STATIC)
add_library(Devex::Platform ALIAS devex_platform)

target_sources(devex_platform
    PRIVATE
        src/platform/Input.cpp
        src/platform/Platform.cpp
        src/platform/SdlWindow.hpp
        src/platform/Window.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/devex/platform/Event.hpp
            include/devex/platform/Input.hpp
            include/devex/platform/Key.hpp
            include/devex/platform/Platform.hpp
            include/devex/platform/Window.hpp
)

target_link_libraries(devex_platform
    PUBLIC
        Devex::Core
        Devex::Math
    PRIVATE
        SDL3::SDL3
)
set_target_properties(devex_platform PROPERTIES
    EXPORT_NAME Platform
    FOLDER "Engine"
)
devex_set_warnings(devex_platform)

# Reflection: field descriptions of engine and game types, registered explicitly.
add_library(devex_reflection STATIC)
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
add_library(devex_serialization STATIC)
add_library(Devex::Serialization ALIAS devex_serialization)

target_sources(devex_serialization
    PRIVATE
        src/serialization/Text.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/devex/serialization/Text.hpp
)

target_link_libraries(devex_serialization PUBLIC Devex::Core)
set_target_properties(devex_serialization PROPERTIES
    EXPORT_NAME Serialization
    FOLDER "Engine"
)
devex_set_warnings(devex_serialization)

# Asset: CPU-side asset data shared by the runtime and the tools.
add_library(devex_asset STATIC)
add_library(Devex::Asset ALIAS devex_asset)

target_sources(devex_asset
    PRIVATE
        src/asset/MeshData.cpp
        src/asset/Primitives.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/devex/asset/AssetId.hpp
            include/devex/asset/MeshData.hpp
            include/devex/asset/Primitives.hpp
)

target_link_libraries(devex_asset
    PUBLIC
        Devex::Core
        Devex::Math
        Devex::Reflection
)
set_target_properties(devex_asset PROPERTIES
    EXPORT_NAME Asset
    FOLDER "Engine"
)
devex_set_warnings(devex_asset)

# AssetImport: converters from source formats such as glTF. Meant for tools, not shipped games.
add_library(devex_asset_import STATIC)
add_library(Devex::AssetImport ALIAS devex_asset_import)

target_sources(devex_asset_import
    PRIVATE
        src/asset/import/GltfImporter.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/devex/asset/import/GltfImporter.hpp
)

target_link_libraries(devex_asset_import
    PUBLIC
        Devex::Asset
    PRIVATE
        fastgltf::fastgltf
)
set_target_properties(devex_asset_import PROPERTIES
    EXPORT_NAME AssetImport
    FOLDER "Engine"
)
devex_set_warnings(devex_asset_import)

# Render: Renderer facade over Vulkan, whose types stay inside src/render/vulkan.
add_library(devex_render STATIC)
add_library(Devex::Render ALIAS devex_render)

target_sources(devex_render
    PRIVATE
        src/render/Renderer.cpp
        src/render/Selection.cpp
        src/render/Selection.hpp
        src/render/vulkan/Commands.cpp
        src/render/vulkan/Commands.hpp
        src/render/vulkan/Device.cpp
        src/render/vulkan/Device.hpp
        src/render/vulkan/GpuData.hpp
        src/render/vulkan/Instance.cpp
        src/render/vulkan/Instance.hpp
        src/render/vulkan/Memory.cpp
        src/render/vulkan/Memory.hpp
        src/render/vulkan/Pipeline.cpp
        src/render/vulkan/Pipeline.hpp
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
            include/devex/render/Gpu.hpp
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

# Scene: entities, components in sparse sets, hierarchy and .dvxscene files.
add_library(devex_scene STATIC)
add_library(Devex::Scene ALIAS devex_scene)

target_sources(devex_scene
    PRIVATE
        src/scene/ComponentRegistry.cpp
        src/scene/Components.cpp
        src/scene/Scene.cpp
        src/scene/SceneSerializer.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/devex/scene/ComponentPool.hpp
            include/devex/scene/ComponentRegistry.hpp
            include/devex/scene/Components.hpp
            include/devex/scene/Entity.hpp
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
    PRIVATE
        Devex::Serialization
)
set_target_properties(devex_scene PROPERTIES
    EXPORT_NAME Scene
    FOLDER "Engine"
)
devex_set_warnings(devex_scene)

# Runtime: application lifecycle, the main loop and the scene to renderer bridge.
add_library(devex_runtime STATIC)
add_library(Devex::Runtime ALIAS devex_runtime)

target_sources(devex_runtime
    PRIVATE
        src/runtime/Application.cpp
        src/runtime/AssetRegistry.cpp
        src/runtime/FixedTimestep.cpp
        src/runtime/SceneExtraction.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/devex/runtime/Application.hpp
            include/devex/runtime/AssetRegistry.hpp
            include/devex/runtime/FixedTimestep.hpp
            include/devex/runtime/SceneExtraction.hpp
)

target_link_libraries(devex_runtime
    PUBLIC
        Devex::Core
        Devex::Platform
        Devex::Render
        Devex::Scene
)
set_target_properties(devex_runtime PROPERTIES
    EXPORT_NAME Runtime
    FOLDER "Engine"
)
devex_set_warnings(devex_runtime)
