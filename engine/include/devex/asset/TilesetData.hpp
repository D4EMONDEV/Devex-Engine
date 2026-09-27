#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/core/Error.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Tilesets: the tiles a tilemap paints its cells with, each a sprite with what the game needs to
// know of it.
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

[[nodiscard]] std::string_view toString(TileCollision collision) noexcept;
[[nodiscard]] std::optional<TileCollision> parseTileCollision(std::string_view text) noexcept;

struct TileData
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

    // The sprite shown at a time, in seconds.
    [[nodiscard]] AssetId spriteAt(double seconds) const noexcept;

    bool operator==(const TileData&) const = default;
};

struct TilesetData
{
    std::vector<TileData> tiles;

    // Null when no tile has the identifier.
    [[nodiscard]] const TileData* find(std::uint32_t id) const noexcept;
    // The identifier a new tile takes: one more than the largest.
    [[nodiscard]] std::uint32_t nextId() const noexcept;

    bool operator==(const TilesetData&) const = default;
};

// Identifiers from 1 to maxTileId, each once, and frame rates that are not negative.
[[nodiscard]] core::Result<void> validate(const TilesetData& tileset);

} // namespace devex::asset
