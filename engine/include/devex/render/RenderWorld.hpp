#pragma once

#include <devex/core/SlotMap.hpp>
#include <devex/math/Math.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace devex::render {

struct MeshTag;
struct TextureTag;
struct MaterialTag;
// Refers to a mesh uploaded with Renderer::createMesh.
using MeshHandle = core::Handle<MeshTag>;
// Refers to a texture uploaded with Renderer::createTexture.
using TextureHandle = core::Handle<TextureTag>;
// Refers to a material created with Renderer::createMaterial.
using MaterialHandle = core::Handle<MaterialTag>;

enum class Tonemapper : std::uint8_t
{
    AgX,
    PbrNeutral,
    Aces,
    None,
};

struct RenderCamera
{
    // World to view transform: the inverse of the camera's world transform.
    math::Mat4 view{1.0f};
    float verticalFov = math::radians(60.0f);
    // Distance of the near plane. There is no far plane: depth is reversed and infinite.
    float nearPlane = 0.1f;
    // Exposure in photographic units: see Camera in the scene module.
    bool autoExposure = true;
    float ev100 = 14.0f;
    float exposureCompensation = 0.0f;
    float minEv100 = -2.0f;
    float maxEv100 = 18.0f;
    float adaptationSpeed = 1.5f;
    Tonemapper tonemapper = Tonemapper::AgX;
};

// The directional light, which may cast shadows.
struct RenderSun
{
    // Direction in which the light travels, in world space.
    math::Vec3 direction{-0.4f, -1.0f, -0.3f};
    // Linear RGB illuminance in lux; zero disables the sun.
    math::Vec3 illuminance{0.0f};
    bool castShadows = true;
    float shadowDistance = 80.0f;
};

enum class LightType : std::uint8_t
{
    Point,
    Spot,
};

struct RenderLight
{
    LightType type = LightType::Point;
    math::Vec3 position{0.0f};
    // Direction in which a spot light shines, normalized.
    math::Vec3 direction{0.0f, 0.0f, -1.0f};
    // Linear RGB luminous intensity in candelas.
    math::Vec3 intensity{0.0f};
    float range = 10.0f;
    // Half angles of a spot light's full and fading cones, in radians.
    float innerAngle = 0.0f;
    float outerAngle = 0.0f;
};

struct RenderEnvironment
{
    // An equirectangular high dynamic range texture; invalid for a uniform sky of `color`.
    TextureHandle sky;
    math::Vec3 color{1.0f};
    // Luminance of a sky texel of value 1, in nits.
    float intensity = 8000.0f;
    // Rotation around the vertical axis, in radians.
    float rotation = 0.0f;
};

// One submesh of a mesh, drawn with a material.
struct MeshInstance
{
    MeshHandle mesh;
    std::uint32_t submesh = 0;
    // An invalid or destroyed material draws with the default material.
    MaterialHandle material;
    math::Mat4 transform{1.0f};
};

// Snapshot of everything the renderer draws in one frame. Gameplay fills it between
// Renderer::beginFrame and Renderer::endFrame, and the renderer never reads gameplay state
// directly, so rendering can later move to its own thread without changing this contract.
struct RenderWorld
{
    RenderCamera camera;
    RenderSun sun;
    std::vector<RenderLight> lights;
    RenderEnvironment environment;
    std::vector<MeshInstance> meshes;

    // Restores the defaults while keeping allocated storage.
    void reset() noexcept
    {
        std::vector<RenderLight> lightStorage = std::move(lights);
        std::vector<MeshInstance> meshStorage = std::move(meshes);
        lightStorage.clear();
        meshStorage.clear();
        *this = RenderWorld{};
        lights = std::move(lightStorage);
        meshes = std::move(meshStorage);
    }
};

} // namespace devex::render
