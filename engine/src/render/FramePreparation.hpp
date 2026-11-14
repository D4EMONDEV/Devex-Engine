#pragma once

// What a renderer prepares on the CPU before it draws a frame, whatever its API: the data the shaders read, packed as
// GpuTypes.hpp lays it out. Each backend then writes it to its buffers and records its own passes.
#include "GpuTypes.hpp"

#include <devex/core/Export.hpp>
#include <devex/math/Math.hpp>
#include <devex/render/Culling.hpp>
#include <devex/render/RenderWorld.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace devex::render {

// The 2D lights of the world that light what the camera sees of the plane of the sprites, at most maxLights2D, and the
// distances of the shadows of the first maxShadowedLights2D that cast some, which a light finds at its shadowOffset.
DEVEX_API void gatherLights2D(const RenderWorld& world, math::Vec2 cameraPosition, const math::Mat4& viewProjection,
                              std::vector<GpuLight2D>& lights, std::vector<float>& shadows);

// Where the values of an instance start, when the frame has them; noInstanceParameters otherwise.
[[nodiscard]] DEVEX_API std::uint32_t instanceParametersOf(const RenderWorld& world, std::uint32_t first) noexcept;

// The air and the fog as the shaders read them, for a camera standing `cameraHeight` meters up, and how high that is in
// the air, in kilometers, which the light of the sun through the air needs.
struct PackedAtmosphere
{
    GpuAtmosphere gpu;
    float altitude = 0.0f;
};
[[nodiscard]] DEVEX_API PackedAtmosphere packAtmosphere(const RenderWorld& world, float cameraHeight);

// The interface of the frame in one list of vertices and one of indices: that of the scene, then each surface, then the
// layer of the tools, which only the tools draw. A surface counts its indices from its own first vertex, placed after what
// came before; where each surface and the tools start among the indices.
struct PackedUi
{
    std::vector<GpuUiVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint32_t> surfaceStarts;
    std::uint32_t toolsStart = 0;
};
DEVEX_API void packUi(const RenderWorld& world, bool tools, PackedUi& packed);

// A sprite, or a whole tilemap, in the order the sprites are drawn: by layer, then by order, then the farthest first.
struct SpriteBatch
{
    std::int32_t layer = 0;
    std::int32_t order = 0;
    float distance = 0.0f;
    // A sprite of RenderWorld::sprites, or a tilemap of RenderWorld::tilemaps.
    bool tilemap = false;
    std::uint32_t source = 0;
    // Where it is among the sprites of the frame.
    std::uint32_t first = 0;
    std::uint32_t count = 0;
};

// The sprites and the tiles the camera sees, in drawing order, as the shaders read them, which of them are outlined, and
// the canvas_item shader each is drawn with.
struct SpriteFrame
{
    std::vector<SpriteBatch> batches;
    std::vector<GpuSprite> sprites;
    std::vector<std::uint8_t> outlined;
    std::vector<ShaderHandle> shaders;
};

// What the backend knows of its own resources: where a texture sits for the shaders (noParticleTexture until it is ready),
// the index of a material, the canvas_item shader of a material, and how far a point is from the camera for sorting.
struct SpriteResources
{
    std::function<std::uint32_t(TextureHandle)> textureSlot;
    std::function<std::uint32_t(MaterialHandle)> materialIndex;
    std::function<ShaderHandle(MaterialHandle)> canvasShader;
    std::function<float(math::Vec3)> sortDistance;
};
DEVEX_API void gatherSprites(const RenderWorld& world, const Frustum& camera, const SpriteResources& resources, SpriteFrame& frame);

} // namespace devex::render
