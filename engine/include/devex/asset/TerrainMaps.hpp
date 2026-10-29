#pragma once

#include <devex/core/Export.hpp>

#include <devex/core/Error.hpp>
#include <devex/math/Math.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// Terrains: the relief of the ground as heights on a square grid, with what is painted on it, which
// the Terrain component lays out over its size in meters.
namespace devex::asset {

// The relief and the paint of a terrain: a binary file, the same bytes as its cooked asset.
inline constexpr std::string_view terrainExtension = ".dvxterrain";

// The layers a terrain paints with, at most.
inline constexpr std::uint32_t maxTerrainLayers = 8;

// The heights of a terrain at the corners of a square grid of cells, and at each of them the share
// of each layer painted there, whether it is a hole and how much grass grows there.
struct DEVEX_API TerrainMaps
{
    // Samples on a side: a power of two plus one, from 33 to 4097.
    std::uint32_t resolution = 0;
    // Meters above the origin of the terrain, row after row along Z, resolution² of them.
    std::vector<float> heights;
    // The share of each layer painted, maxTerrainLayers bytes per sample (255 for all of it). Where
    // they add up to less than 255, the rules of the terrain decide the rest.
    std::vector<std::uint8_t> paint;
    // Above 127 where the ground is a hole, per sample.
    std::vector<std::uint8_t> holes;
    // The density of the grass, per sample.
    std::vector<std::uint8_t> grass;

    [[nodiscard]] std::size_t sampleCount() const noexcept
    {
        return std::size_t{resolution} * resolution;
    }

    bool operator==(const TerrainMaps&) const = default;
};

[[nodiscard]] DEVEX_API core::Result<void> validate(const TerrainMaps& terrain);

// A flat terrain at a height, unpainted, without holes or grass.
[[nodiscard]] DEVEX_API TerrainMaps makeFlatTerrain(std::uint32_t resolution, float height = 0.0f);

// The height of the ground at a point of a terrain of `size` meters, x and z from 0 to size, between
// its samples; the edges continue outside.
[[nodiscard]] DEVEX_API float terrainHeight(const TerrainMaps& terrain, float size, float x, float z) noexcept;
// Its normal there, from the slopes between the samples around.
[[nodiscard]] DEVEX_API math::Vec3 terrainNormal(const TerrainMaps& terrain, float size, float x, float z) noexcept;
// Whether the ground is a hole at a point.
[[nodiscard]] DEVEX_API bool isTerrainHole(const TerrainMaps& terrain, float size, float x, float z) noexcept;
// How far along a ray, in the space of the terrain, it first meets the ground within maxDistance;
// nothing when it passes over, under or through a hole.
[[nodiscard]] DEVEX_API std::optional<float> raycastTerrain(const TerrainMaps& terrain, float size, math::Vec3 origin,
                                                            math::Vec3 direction, float maxDistance) noexcept;

// The lowest and the highest height of each block of `cells` cells on a side, row after row: what
// the renderer culls and picks the detail of the terrain by.
[[nodiscard]] DEVEX_API std::vector<math::Vec2> terrainHeightRanges(const TerrainMaps& terrain, std::uint32_t cells);

// What a brush does to the samples under it.
enum class TerrainTool : std::uint8_t
{
    Raise,
    Lower,
    // Towards the average of the samples around.
    Smooth,
    // Towards the height of the brush.
    Flatten,
    // Bumps that stay where they are, as a noise of the place.
    Noise,
    // The layer of the brush.
    Paint,
    Hole,
    Grass,
};

// A brush, in the space of the terrain: its center in meters, its radius, and how much one stroke
// step does at its center. Its effect falls off from (1 - softness) of its radius to its edge.
struct DEVEX_API TerrainBrush
{
    TerrainTool tool = TerrainTool::Raise;
    math::Vec2 center{0.0f};
    float radius = 5.0f;
    float softness = 0.5f;
    // Raise, Lower and Noise: meters; Smooth, Flatten, Paint and Grass: a share from 0 to 1 of the
    // way to go; Hole: any amount makes holes.
    float amount = 0.1f;
    // Flatten: the height to reach.
    float height = 0.0f;
    // Paint: the layer.
    std::uint32_t layer = 0;
    // Paint, Hole and Grass: take away rather than add.
    bool erase = false;
};

// The samples a change touched, as a rectangle of them; empty when none.
struct DEVEX_API TerrainRegion
{
    std::uint32_t x = 0;
    std::uint32_t z = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] bool empty() const noexcept
    {
        return width == 0 || height == 0;
    }
    // The rectangle covering both.
    [[nodiscard]] TerrainRegion merged(const TerrainRegion& other) const noexcept;

    bool operator==(const TerrainRegion&) const = default;
};

// Applies one step of a brush to the terrain of `size` meters, and returns the samples it changed.
DEVEX_API TerrainRegion applyTerrainBrush(TerrainMaps& terrain, float size, const TerrainBrush& brush);

// A copy of the samples of a region, of every kind, and puts them back: what the editor undoes.
struct DEVEX_API TerrainPatch
{
    TerrainRegion region;
    std::vector<float> heights;
    std::vector<std::uint8_t> paint;
    std::vector<std::uint8_t> holes;
    std::vector<std::uint8_t> grass;
};
[[nodiscard]] DEVEX_API TerrainPatch copyTerrainRegion(const TerrainMaps& terrain, const TerrainRegion& region);
DEVEX_API void pasteTerrainPatch(TerrainMaps& terrain, const TerrainPatch& patch);

} // namespace devex::asset
