#pragma once

#include <devex/core/Export.hpp>

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

// How the jagged edges of the image are smoothed.
enum class Antialiasing : std::uint8_t
{
    None,
    // Every frame is drawn a fraction of a pixel aside and mixed with the previous ones.
    Temporal,
};

enum class Tonemapper : std::uint8_t
{
    AgX,
    PbrNeutral,
    Aces,
    None,
};

enum class Projection : std::uint8_t
{
    Perspective,
    // Things keep their size whatever their distance, as in 2D games.
    Orthographic,
};

struct DEVEX_API RenderCamera
{
    // World to view transform: the inverse of the camera's world transform.
    math::Mat4 view{1.0f};
    Projection projection = Projection::Perspective;
    float verticalFov = math::radians(60.0f);
    // Orthographic: half the height of the image, in meters.
    float orthographicSize = 5.0f;
    // Distance of the near plane. Depth is reversed: a perspective has no far plane and sees
    // infinitely far, an orthographic view stops at its far plane.
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    // Exposure in photographic units: see Camera in the scene module.
    bool autoExposure = true;
    float ev100 = 14.0f;
    float exposureCompensation = 0.0f;
    float minEv100 = -2.0f;
    float maxEv100 = 18.0f;
    float adaptationSpeed = 1.5f;
    Tonemapper tonemapper = Tonemapper::AgX;
    Antialiasing antialiasing = Antialiasing::Temporal;
    // Darkens the ambient light in the corners of the scene; 0 turns it off.
    float ambientOcclusion = 1.0f;
    // How far, in meters, a surface looks for what hides it.
    float ambientOcclusionRadius = 0.5f;
    // How much of the halo of bright things is added to the image; 0 turns it off.
    float bloom = 0.05f;
    // The brightness, after exposure, a pixel must pass before it glows.
    float bloomThreshold = 1.0f;
    // Darkens the corners of the image, from 0 to 1.
    float vignette = 0.0f;
    // Adds noise to the image, as film does.
    float grain = 0.0f;
    // Pulls red and blue apart towards the edges of the image, as a lens does.
    float chromaticAberration = 0.0f;
    // A table of colors kept as a strip of squares, applied to the tonemapped image. The texture
    // must be imported without the sRGB encoding, so that its values pass through untouched.
    TextureHandle colorTable;
};

// The directional light, which may cast shadows.
struct DEVEX_API RenderSun
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

struct DEVEX_API RenderLight
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
    // Whether the light darkens what stands behind what it lights. A spot takes one view of the
    // shadow atlas, a point light six; lights that do not fit keep their light without a shadow.
    bool castShadows = false;
};

struct DEVEX_API RenderEnvironment
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
struct DEVEX_API MeshInstance
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

// A particle as the renderer draws it: a quad facing the camera, or lying as its batch says.
struct DEVEX_API RenderParticle
{
    math::Vec3 position{0.0f};
    // Width in meters.
    float size = 1.0f;
    // Linear color relative to the exposure, as the emission of materials, with straight alpha.
    math::Vec4 color{1.0f};
    // For stretched particles: from the tail of the particle to its head, in the world.
    math::Vec3 stretch{0.0f};
    // Around the axis the particle faces, in radians.
    float rotation = 0.0f;
    // The frame of the sheet of its texture.
    float frame = 0.0f;
};

// A point of a ribbon: a trail behind a particle or an entity.
struct DEVEX_API RenderTrailPoint
{
    math::Vec3 position{0.0f};
    float width = 0.0f;
    math::Vec4 color{1.0f};
    // Along the ribbon, from its head towards its tail, normalized.
    math::Vec3 direction{0.0f, 0.0f, 1.0f};
    // Across the texture, 0 at the head of the ribbon and 1 at its tail.
    float u = 0.0f;
};

enum class ParticleBlend : std::uint8_t
{
    // Covers by alpha; the particles of a batch are drawn from the farthest.
    Alpha,
    // Adds its light, in any order.
    Additive,
};

enum class ParticleFacing : std::uint8_t
{
    Billboard,
    Stretched,
    Horizontal,
    Vertical,
};

// Particles or ribbons drawn together, sorted as a whole with the blended surfaces of the scene.
struct DEVEX_API ParticleDraw
{
    // Ribbons: segments of RenderWorld::trailSegments; otherwise RenderWorld::particles.
    bool ribbons = false;
    std::uint32_t first = 0;
    std::uint32_t count = 0;
    // Invalid draws a soft disc for particles, and plain ribbons.
    TextureHandle texture;
    ParticleBlend blend = ParticleBlend::Alpha;
    ParticleFacing facing = ParticleFacing::Billboard;
    // The frames of the texture, side by side.
    std::uint32_t sheetColumns = 1;
    std::uint32_t sheetRows = 1;
    // Lit as a matte surface by the sun, the sky and the lights; otherwise shines alone.
    bool lit = false;
    // Meters over which particles fade into the surfaces they cross.
    float softness = 0.0f;
    // Where the batch stands, to order it among the blended surfaces.
    math::Vec3 center{0.0f};
};

enum class SpriteMode : std::uint8_t
{
    // Its rectangle stretched over the quad.
    Simple,
    // Its borders kept at their size and its middle stretched.
    Sliced,
    // Its borders kept at their size and its middle repeated.
    Tiled,
};

// A rectangle of a texture lying in the XY plane of its transform and facing +Z, drawn among the
// blended surfaces: by layer, then by order, then from the farthest.
struct DEVEX_API RenderSprite
{
    math::Mat4 transform{1.0f};
    // The rectangle drawn, in meters, and the point of it at the origin of the transform, from its
    // bottom-left corner (0, 0) to its top-right one (1, 1).
    math::Vec2 size{1.0f};
    math::Vec2 pivot{0.5f};
    // Texture coordinates of the top-left corner of the sprite, then of its bottom-right one.
    math::Vec4 uvRect{0.0f, 0.0f, 1.0f, 1.0f};
    // The size of the sprite at its pixels, and the borders a sliced or tiled one keeps at their
    // size: left, bottom, right and top, in meters.
    math::Vec2 naturalSize{1.0f};
    math::Vec4 border{0.0f};
    // Linear color with straight alpha, relative to the exposure unless lit.
    math::Vec4 color{1.0f};
    // Invalid fills the rectangle with the color.
    TextureHandle texture;
    SpriteMode mode = SpriteMode::Simple;
    // Mirrors the sprite around its pivot.
    bool flipX = false;
    bool flipY = false;
    bool additive = false;
    // Lit as a matte surface by the sun, the sky and the lights; otherwise shines alone.
    bool lit = false;
    // The sorting layer, counted from the one of the blended surfaces and the particles, then the
    // order within it: higher ones draw over lower ones, whatever their distance.
    std::int32_t layer = 0;
    std::int32_t order = 0;
    // As for a mesh instance: reported by picking, and outlined for the selection.
    std::uint32_t objectId = 0;
    bool outlined = false;
};

// A tile of a tilemap: a rectangle of a texture filling a cell.
struct DEVEX_API RenderTile
{
    math::IVec2 cell{0};
    // Texture coordinates of the top-left corner, then of the bottom-right one; swapped to mirror.
    math::Vec4 uvRect{0.0f, 0.0f, 1.0f, 1.0f};
    TextureHandle texture;
};

// A grid of tiles in the XY plane of its transform, cell (x, y) covering [x, x + 1) by [y, y + 1)
// cells from its origin. It sorts among the sprites as one of them.
struct DEVEX_API RenderTilemap
{
    math::Mat4 transform{1.0f};
    math::Vec2 cellSize{1.0f};
    // Linear color with straight alpha, relative to the exposure unless lit.
    math::Vec4 color{1.0f};
    // Its tiles, in RenderWorld::tiles.
    std::uint32_t firstTile = 0;
    std::uint32_t tileCount = 0;
    bool lit = false;
    std::int32_t layer = 0;
    std::int32_t order = 0;
    std::uint32_t objectId = 0;
    bool outlined = false;
};

// A vertex of the lines and triangles the tools draw over the scene, such as grids and gizmos.
struct DEVEX_API OverlayVertex
{
    math::Vec3 position{0.0f};
    // Linear RGB and straight alpha, blended over the displayed image.
    math::Vec4 color{1.0f};
};

// A vertex of the interface, in pixels of the image, with straight alpha.
struct DEVEX_API UiVertex
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
struct DEVEX_API UiDraw
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
    // What the batch is cut to, in pixels: left, top, right and bottom. An empty rectangle, with
    // its right at or before its left, draws the whole image.
    math::Vec4 clip{0.0f};
};

// An interface drawn into an image of its own rather than over the scene, which the tools show as a
// texture (Renderer::uiSurfaceTexture): the panels of the editor made with the interface of the
// engine. Positions are in pixels of that image; indices count from its own first vertex.
struct DEVEX_API UiSurface
{
    std::uint32_t id = 0;
    math::Extent2D size;
    // Linear RGB, what the image holds under the interface.
    math::Vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};
    std::vector<UiVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<UiDraw> draws;
};

// Asks which objects are visible in a rectangle of the scene image, one pixel for a click.
struct DEVEX_API PickRequest
{
    // The top-left corner of the rectangle, from the top-left corner of the scene image.
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    // Returned with the result, to match it with its request.
    std::uint64_t id = 0;
    std::uint32_t width = 1;
    std::uint32_t height = 1;
};

struct DEVEX_API PickResult
{
    std::uint64_t request = 0;
    // MeshInstance::objectId of the surface at the middle of the rectangle, 0 when none is there.
    std::uint32_t objectId = 0;
    // Every object visible in the rectangle, once each, in increasing order. A large rectangle is
    // looked at in fewer pixels than it covers, which may miss objects smaller than a few pixels.
    std::vector<std::uint32_t> objectIds;
};

// A small picture of a frame as the game shows it, without its interface nor the tools, such as
// the thumbnail of a save.
struct DEVEX_API CapturedImage
{
    std::uint64_t request = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Four bytes per pixel, red first, in sRGB as the display shows them; opaque.
    std::vector<std::uint8_t> rgba;
};

// Snapshot of everything the renderer draws in one frame. Gameplay fills it between
// Renderer::beginFrame and Renderer::endFrame, and the renderer never reads gameplay state
// directly, so rendering can later move to its own thread without changing this contract.
struct DEVEX_API RenderWorld
{
    RenderCamera camera;
    RenderSun sun;
    std::vector<RenderLight> lights;
    RenderEnvironment environment;
    std::vector<MeshInstance> meshes;
    // The bones of the skinned instances of the frame, each already holding the transform from the
    // space of its mesh to the world.
    std::vector<math::Mat4> boneMatrices;
    // Particles and ribbons, drawn batch by batch among the blended surfaces. A ribbon segment is
    // the index of the trail point it starts at; it ends at the next one.
    std::vector<RenderParticle> particles;
    std::vector<RenderTrailPoint> trailPoints;
    std::vector<std::uint32_t> trailSegments;
    std::vector<ParticleDraw> particleDraws;
    std::vector<RenderSprite> sprites;
    std::vector<RenderTilemap> tilemaps;
    std::vector<RenderTile> tiles;

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
    // Interfaces drawn into images of their own, for the tools; only drawn with them.
    std::vector<UiSurface> uiSurfaces;

    // Restores the defaults while keeping allocated storage.
    void reset() noexcept
    {
        std::vector<RenderLight> lightStorage = std::move(lights);
        std::vector<MeshInstance> meshStorage = std::move(meshes);
        std::vector<math::Mat4> boneStorage = std::move(boneMatrices);
        std::vector<RenderParticle> particleStorage = std::move(particles);
        std::vector<RenderTrailPoint> trailPointStorage = std::move(trailPoints);
        std::vector<std::uint32_t> trailSegmentStorage = std::move(trailSegments);
        std::vector<ParticleDraw> particleDrawStorage = std::move(particleDraws);
        std::vector<RenderSprite> spriteStorage = std::move(sprites);
        std::vector<RenderTilemap> tilemapStorage = std::move(tilemaps);
        std::vector<RenderTile> tileStorage = std::move(tiles);
        spriteStorage.clear();
        tilemapStorage.clear();
        tileStorage.clear();
        particleStorage.clear();
        trailPointStorage.clear();
        trailSegmentStorage.clear();
        particleDrawStorage.clear();
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
        particles = std::move(particleStorage);
        trailPoints = std::move(trailPointStorage);
        trailSegments = std::move(trailSegmentStorage);
        particleDraws = std::move(particleDrawStorage);
        sprites = std::move(spriteStorage);
        tilemaps = std::move(tilemapStorage);
        tiles = std::move(tileStorage);
        sceneLines = std::move(sceneLineStorage);
        overlayLines = std::move(overlayLineStorage);
        overlayTriangles = std::move(overlayTriangleStorage);
        uiVertices = std::move(uiVertexStorage);
        uiIndices = std::move(uiIndexStorage);
        uiDraws = std::move(uiDrawStorage);
    }
};

} // namespace devex::render
