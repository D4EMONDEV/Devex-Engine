#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/TilesetData.hpp>
#include <devex/math/Math.hpp>
#include <devex/scene/TilemapComponents.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

// Terrains, painted as Godot paints them: a brush names a terrain for some cells, and the tile of
// each cell is the one of the terrain set whose middle, sides and corners best match the cells
// around it; the cells around change their tiles to match in turn.
namespace devex::scene {

enum class TerrainPaint : std::uint8_t
{
    // The cells painted join each other and the cells of the same terrain around them.
    Connect,
    // Each cell painted joins the one before it and the one after it, in the order given, and
    // nothing else: roads, rivers.
    Path,
};

// What a cell shows of a terrain set: the terrain of the middle of its tile and of its sides and
// corners, as the cell mirrors the tile.
struct DEVEX_API TerrainPattern
{
    std::int32_t terrain = asset::noTerrain;
    asset::TerrainBits bits = asset::noTerrainBits;

    bool operator==(const TerrainPattern&) const = default;
};

// The pattern of a cell for a terrain set: the empty pattern for an empty cell, nothing for the
// tile of another set or of none.
[[nodiscard]] DEVEX_API std::optional<TerrainPattern> terrainPattern(const asset::TilesetData& tileset, std::int32_t terrainSet,
                                                                     std::uint16_t cell);
// The terrain set of a cell's tile and the terrain of its middle; noTerrain for both outside the sets.
[[nodiscard]] DEVEX_API std::pair<std::int32_t, std::int32_t> terrainOf(const asset::TilesetData& tileset, std::uint16_t cell);

// Paints the cells with a terrain of a terrain set, or empties them with noTerrain, and chooses the
// tiles of these cells and of the cells around them. Among the tiles that fit a cell equally, the
// probability of each chooses, the same way for the same cell. A terrain without tiles paints
// nothing. Returns the cells whose tile changed.
DEVEX_API std::vector<math::IVec2> paintTerrain(TileGrid& grid, const asset::TilesetData& tileset, std::span<const math::IVec2> cells,
                                                std::int32_t terrainSet, std::int32_t terrain,
                                                TerrainPaint mode = TerrainPaint::Connect);
// The same on the cells of a tilemap, whose blocks are written again when a cell changed.
DEVEX_API std::vector<math::IVec2> paintTerrain(Tilemap& tilemap, const asset::TilesetData& tileset, std::span<const math::IVec2> cells,
                                                std::int32_t terrainSet, std::int32_t terrain,
                                                TerrainPaint mode = TerrainPaint::Connect);

} // namespace devex::scene
