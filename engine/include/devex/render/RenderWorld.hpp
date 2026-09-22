#pragma once

#include <devex/core/SlotMap.hpp>
#include <devex/math/Math.hpp>

#include <cstdint>
#include <optional>
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
    // Bones of a skinned mesh, in RenderWorld::boneMatrices. The transform is then the one of the
    // space the bones are given in, usually the identity of the world.
    std::uint32_t firstBone = 0;
    std::uint32_t boneCount = 0;
    // Reported by picking where the instance is visible; 0 for none.
    std::uint32_t objectId = 0;
    // Draws an outline around the instance, as the editor does for the selection.
    bool outlined = false;
};

// A vertex of the lines and triangles the tools draw over the scene, such as grids and gizmos.
struct OverlayVertex
{
    math::Vec3 position{0.0f};
    // Linear RGB and straight alpha, blended over the displayed image.
    math::Vec4 color{1.0f};
};

// A vertex of the interface, in pixels of the image, with straight alpha.
struct UiVertex
{
    math::Vec2 position{0.0f};
    math::Vec2 uv{0.0f};
    math::Vec4 color{1.0f};
};

enum class UiDrawKind : std::uint8_t
{
    // A rectangle, filled with its color and its texture when it has one.
    Quad,
    // The same, with its corners rounded.
    RoundedQuad,
    // Letters read from the atlas of distances of a font.
    Text,
};

// One batch of the interface: the triangles that share a texture and a shape.
struct UiDraw
{
    UiDrawKind kind = UiDrawKind::Quad;
    // Invalid draws the color alone.
    TextureHandle texture;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    // The rectangle the corners are rounded on, and by how much, in pixels.
    math::Vec4 rect{0.0f};
    float radius = 0.0f;
    // For text: how many texels of the atlas one pixel covers, which keeps the edges sharp.
    float sharpness = 1.0f;
};

// Asks which object is visible at a pixel of the scene image.
struct PickRequest
{
    // From the top-left corner of the scene image.
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    // Returned with the result, to match it with its request.
    std::uint64_t id = 0;
};

struct PickResult
{
    std::uint64_t request = 0;
    // MeshInstance::objectId of the surface at the pixel, 0 when none is there.
    std::uint32_t objectId = 0;
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
    // The bones of the skinned instances of the frame, each already holding the transform from the
    // space of its mesh to the world.
    std::vector<math::Mat4> boneMatrices;

    // Size in pixels of the image the scene is drawn into for the tools, which show it with
    // Renderer::viewportTexture. Zero draws the scene over the whole window.
    math::Extent2D viewport;
    // Pairs of vertices drawn as lines over the scene, hidden behind nearer surfaces.
    std::vector<OverlayVertex> sceneLines;
    // Pairs of vertices drawn as lines over everything.
    std::vector<OverlayVertex> overlayLines;
    // Triples of vertices drawn as triangles over everything, after the lines.
    std::vector<OverlayVertex> overlayTriangles;
    // Answered some frames later by Renderer::takePickResults.
    std::optional<PickRequest> pick;

    // The interface, drawn over everything in the order it is filled. Positions are in pixels of
    // the image, from its top left corner.
    std::vector<UiVertex> uiVertices;
    std::vector<std::uint32_t> uiIndices;
    std::vector<UiDraw> uiDraws;

    // Restores the defaults while keeping allocated storage.
    void reset() noexcept
    {
        std::vector<RenderLight> lightStorage = std::move(lights);
        std::vector<MeshInstance> meshStorage = std::move(meshes);
        std::vector<math::Mat4> boneStorage = std::move(boneMatrices);
        std::vector<OverlayVertex> sceneLineStorage = std::move(sceneLines);
        std::vector<OverlayVertex> overlayLineStorage = std::move(overlayLines);
        std::vector<OverlayVertex> overlayTriangleStorage = std::move(overlayTriangles);
        std::vector<UiVertex> uiVertexStorage = std::move(uiVertices);
        std::vector<std::uint32_t> uiIndexStorage = std::move(uiIndices);
        std::vector<UiDraw> uiDrawStorage = std::move(uiDraws);
        uiVertexStorage.clear();
        uiIndexStorage.clear();
        uiDrawStorage.clear();
        lightStorage.clear();
        meshStorage.clear();
        boneStorage.clear();
        sceneLineStorage.clear();
        overlayLineStorage.clear();
        overlayTriangleStorage.clear();
        *this = RenderWorld{};
        lights = std::move(lightStorage);
        meshes = std::move(meshStorage);
        boneMatrices = std::move(boneStorage);
        sceneLines = std::move(sceneLineStorage);
        overlayLines = std::move(overlayLineStorage);
        overlayTriangles = std::move(overlayTriangleStorage);
        uiVertices = std::move(uiVertexStorage);
        uiIndices = std::move(uiIndexStorage);
        uiDraws = std::move(uiDrawStorage);
    }
};

} // namespace devex::render
