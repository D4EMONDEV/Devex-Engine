#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/AssetId.hpp>
#include <devex/core/Error.hpp>
#include <devex/math/Math.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Tilesets: the tiles a tilemap paints its cells with, each a sprite with what the game needs to
// know of it, and the terrains that choose them by their neighbours, as Godot's do.
namespace devex::asset {

inline constexpr std::string_view tilesetExtension = ".dvxtileset";

// The largest identifier of a tile: cells keep it in the 14 lowest bits of a 16-bit value.
inline constexpr std::uint32_t maxTileId = 0x3FFF;

// What a tile is to the game, for its code now and for 2D physics later. The values are stored in
// cooked files: never reorder them.
enum class TileCollision : std::uint8_t
{
    // Walked through.
    None = 0,
    // Solid on every side: ground, walls.
    Full = 1,
    // Stands under what lands on it from above, and lets through what comes from below: ledges.
    Top = 2,
};

[[nodiscard]] DEVEX_API std::string_view toString(TileCollision collision) noexcept;
[[nodiscard]] DEVEX_API std::optional<TileCollision> parseTileCollision(std::string_view text) noexcept;

// Which neighbours the tiles of a terrain set match. The values are stored in cooked files: never
// reorder them.
enum class TerrainMode : std::uint8_t
{
    // The sides and the corners: inner corners have tiles of their own, 47 for a whole terrain.
    CornersAndSides = 0,
    // The corners only: terrains meet across the corners of the cells, 16 tiles for a whole one.
    Corners = 1,
    // The sides only: 16 tiles for a whole terrain.
    Sides = 2,
};

[[nodiscard]] DEVEX_API std::string_view toString(TerrainMode mode) noexcept;
[[nodiscard]] DEVEX_API std::optional<TerrainMode> parseTerrainMode(std::string_view text) noexcept;

// The sides and corners of a tile, counterclockwise from the right, y up: sides are even.
enum class TileNeighbor : std::uint8_t
{
    Right = 0,
    TopRight = 1,
    Top = 2,
    TopLeft = 3,
    Left = 4,
    BottomLeft = 5,
    Bottom = 6,
    BottomRight = 7,
};

inline constexpr std::size_t tileNeighborCount = 8;

// The cell across a side or a corner, from the cell.
[[nodiscard]] DEVEX_API math::IVec2 neighborOffset(TileNeighbor neighbor) noexcept;
// Whether the tiles of a terrain set in that mode name the terrain at that side or corner.
[[nodiscard]] DEVEX_API bool matchesNeighbor(TerrainMode mode, TileNeighbor neighbor) noexcept;

// No terrain: an empty side of a tile, or a tile outside the terrain sets.
inline constexpr std::int32_t noTerrain = -1;
// The terrains of the sides and corners of a tile, by TileNeighbor.
using TerrainBits = std::array<std::int32_t, tileNeighborCount>;
inline constexpr TerrainBits noTerrainBits{noTerrain, noTerrain, noTerrain, noTerrain,
                                           noTerrain, noTerrain, noTerrain, noTerrain};

// Grass, dirt, water: what a terrain brush paints, and the colour that shows it in the editor.
struct DEVEX_API TerrainData
{
    std::string name;
    math::Vec4 color{1.0f};

    bool operator==(const TerrainData&) const = default;
};

// Terrains whose tiles join each other: a tile of the set names a terrain of it for its middle and
// for each side and corner, and the brushes choose the tile whose names match the cells around.
struct DEVEX_API TerrainSetData
{
    TerrainMode mode = TerrainMode::CornersAndSides;
    // Whether the brushes may mirror the tiles, so that an edge drawn once serves both sides.
    bool mirrorX = false;
    bool mirrorY = false;
    std::vector<TerrainData> terrains;

    bool operator==(const TerrainSetData&) const = default;
};

// At most this many terrain sets, and terrains in a set.
inline constexpr std::size_t maxTerrainSets = 64;
inline constexpr std::size_t maxTerrains = 64;
struct DEVEX_API TileData
{
    // From 1 to maxTileId, kept when tiles are added or removed: cells hold it.
    std::uint32_t id = 0;
    AssetId sprite;
    TileCollision collision = TileCollision::None;
    // An animated tile shows these sprites in turn, fps a second, in place of its sprite: water,
    // lava, torches.
    std::vector<AssetId> frames;
    float fps = 0.0f;
    // Anything the game reads of the tile, such as "water" or "damage=5".
    std::string data;
    // The terrain set of the tile, noTerrain for none; the terrain of its middle, and of its sides and
    // corners, which the mode of the set reads.
    std::int32_t terrainSet = noTerrain;
    std::int32_t terrain = noTerrain;
    TerrainBits terrainBits = noTerrainBits;
    // How often the terrain brushes choose the tile among those that fit the same place: variants.
    float probability = 1.0f;

    // The sprite shown at a time, in seconds.
    [[nodiscard]] AssetId spriteAt(double seconds) const noexcept;

    bool operator==(const TileData&) const = default;
};

struct DEVEX_API TilesetData
{
    std::vector<TileData> tiles;
    std::vector<TerrainSetData> terrainSets;

    // Null when no tile has the identifier.
    [[nodiscard]] const TileData* find(std::uint32_t id) const noexcept;
    // The identifier a new tile takes: one more than the largest.
    [[nodiscard]] std::uint32_t nextId() const noexcept;
    // The terrain set and the terrain of that name, the first one found.
    [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> findTerrain(std::string_view name) const noexcept;
    // Null when the set or the terrain does not exist.
    [[nodiscard]] const TerrainSetData* terrainSet(std::int32_t set) const noexcept;
    [[nodiscard]] const TerrainData* terrain(std::int32_t set, std::int32_t terrain) const noexcept;

    // Remove a set or a terrain, and what the tiles name of it: the tiles keep the others.
    void removeTerrainSet(std::int32_t set);
    void removeTerrain(std::int32_t set, std::int32_t terrain);

    bool operator==(const TilesetData&) const = default;
};

// Identifiers from 1 to maxTileId, each once, frame rates and probabilities that are not negative,
// and terrains that exist in the set of their tile.
[[nodiscard]] DEVEX_API core::Result<void> validate(const TilesetData& tileset);

// Leaves out what a tile names of its terrain set that the mode of the set does not read, and the
// terrains of a tile outside the sets.
DEVEX_API void normalizeTerrains(TileData& tile, const TilesetData& tileset);

} // namespace devex::asset
