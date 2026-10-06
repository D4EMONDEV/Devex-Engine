#include <devex/asset/TilesetData.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>

namespace devex::asset {
namespace {

constexpr std::array<std::pair<TileCollision, std::string_view>, 3> collisionNames{{
    {TileCollision::None, "none"},
    {TileCollision::Full, "full"},
    {TileCollision::Top, "top"},
}};

constexpr std::array<std::pair<TerrainMode, std::string_view>, 3> modeNames{{
    {TerrainMode::CornersAndSides, "corners_and_sides"},
    {TerrainMode::Corners, "corners"},
    {TerrainMode::Sides, "sides"},
}};

[[nodiscard]] bool inRange(std::int32_t value, std::size_t count) noexcept
{
    return value == noTerrain || (value >= 0 && static_cast<std::size_t>(value) < count);
}

} // namespace

std::string_view toString(TileCollision collision) noexcept
{
    for (const auto& [value, name] : collisionNames)
    {
        if (value == collision)
        {
            return name;
        }
    }
    return "none";
}

std::optional<TileCollision> parseTileCollision(std::string_view text) noexcept
{
    for (const auto& [value, name] : collisionNames)
    {
        if (name == text)
        {
            return value;
        }
    }
    return std::nullopt;
}

std::string_view toString(TerrainMode mode) noexcept
{
    for (const auto& [value, name] : modeNames)
    {
        if (value == mode)
        {
            return name;
        }
    }
    return "corners_and_sides";
}

std::optional<TerrainMode> parseTerrainMode(std::string_view text) noexcept
{
    for (const auto& [value, name] : modeNames)
    {
        if (name == text)
        {
            return value;
        }
    }
    return std::nullopt;
}

math::IVec2 neighborOffset(TileNeighbor neighbor) noexcept
{
    static constexpr std::array<math::IVec2, tileNeighborCount> offsets{
        math::IVec2{1, 0}, math::IVec2{1, 1}, math::IVec2{0, 1}, math::IVec2{-1, 1},
        math::IVec2{-1, 0}, math::IVec2{-1, -1}, math::IVec2{0, -1}, math::IVec2{1, -1}};
    return offsets[static_cast<std::size_t>(neighbor) % tileNeighborCount];
}

bool matchesNeighbor(TerrainMode mode, TileNeighbor neighbor) noexcept
{
    const bool side = static_cast<std::uint8_t>(neighbor) % 2 == 0;
    switch (mode)
    {
    case TerrainMode::CornersAndSides:
        return true;
    case TerrainMode::Corners:
        return !side;
    case TerrainMode::Sides:
        return side;
    }
    return false;
}

AssetId TileData::spriteAt(double seconds) const noexcept
{
    if (frames.empty() || !(fps > 0.0f))
    {
        return sprite;
    }
    const auto frame = static_cast<std::int64_t>(std::floor(std::max(seconds, 0.0) * fps));
    return frames[static_cast<std::size_t>(frame % static_cast<std::int64_t>(frames.size()))];
}

const TileData* TilesetData::find(std::uint32_t id) const noexcept
{
    const auto found = std::ranges::find(tiles, id, &TileData::id);
    return found != tiles.end() ? &*found : nullptr;
}

std::uint32_t TilesetData::nextId() const noexcept
{
    std::uint32_t largest = 0;
    for (const TileData& tile : tiles)
    {
        largest = std::max(largest, tile.id);
    }
    return largest + 1;
}

std::optional<std::pair<std::int32_t, std::int32_t>> TilesetData::findTerrain(std::string_view name) const noexcept
{
    for (std::size_t set = 0; set < terrainSets.size(); ++set)
    {
        const std::vector<TerrainData>& terrains = terrainSets[set].terrains;
        for (std::size_t index = 0; index < terrains.size(); ++index)
        {
            if (terrains[index].name == name)
            {
                return std::pair{static_cast<std::int32_t>(set), static_cast<std::int32_t>(index)};
            }
        }
    }
    return std::nullopt;
}

const TerrainSetData* TilesetData::terrainSet(std::int32_t set) const noexcept
{
    return set >= 0 && static_cast<std::size_t>(set) < terrainSets.size() ? &terrainSets[static_cast<std::size_t>(set)]
                                                                           : nullptr;
}

const TerrainData* TilesetData::terrain(std::int32_t set, std::int32_t terrain) const noexcept
{
    const TerrainSetData* const found = terrainSet(set);
    return found != nullptr && terrain >= 0 && static_cast<std::size_t>(terrain) < found->terrains.size()
               ? &found->terrains[static_cast<std::size_t>(terrain)]
               : nullptr;
}

void TilesetData::removeTerrainSet(std::int32_t set)
{
    if (terrainSet(set) == nullptr)
    {
        return;
    }
    terrainSets.erase(terrainSets.begin() + set);
    for (TileData& tile : tiles)
    {
        if (tile.terrainSet == set)
        {
            tile.terrainSet = noTerrain;
            tile.terrain = noTerrain;
            tile.terrainBits = noTerrainBits;
        }
        else if (tile.terrainSet > set)
        {
            --tile.terrainSet;
        }
    }
}

void TilesetData::removeTerrain(std::int32_t set, std::int32_t terrain)
{
    if (this->terrain(set, terrain) == nullptr)
    {
        return;
    }
    std::vector<TerrainData>& terrains = terrainSets[static_cast<std::size_t>(set)].terrains;
    terrains.erase(terrains.begin() + terrain);
    const auto shift = [terrain](std::int32_t& value) {
        if (value == terrain)
        {
            value = noTerrain;
        }
        else if (value > terrain)
        {
            --value;
        }
    };
    for (TileData& tile : tiles)
    {
        if (tile.terrainSet != set)
        {
            continue;
        }
        shift(tile.terrain);
        for (std::int32_t& bit : tile.terrainBits)
        {
            shift(bit);
        }
    }
}

void normalizeTerrains(TileData& tile, const TilesetData& tileset)
{
    const TerrainSetData* const set = tileset.terrainSet(tile.terrainSet);
    if (set == nullptr)
    {
        tile.terrainSet = noTerrain;
        tile.terrain = noTerrain;
        tile.terrainBits = noTerrainBits;
        return;
    }
    for (std::size_t index = 0; index < tileNeighborCount; ++index)
    {
        if (!matchesNeighbor(set->mode, static_cast<TileNeighbor>(index)))
        {
            tile.terrainBits[index] = noTerrain;
        }
    }
}

core::Result<void> validate(const TilesetData& tileset)
{
    if (tileset.terrainSets.size() > maxTerrainSets)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "a tileset has at most {} terrain sets", maxTerrainSets);
    }
    for (const TerrainSetData& set : tileset.terrainSets)
    {
        if (set.terrains.size() > maxTerrains)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "a terrain set has at most {} terrains", maxTerrains);
        }
        if (set.mode > TerrainMode::Sides)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "unknown terrain mode");
        }
    }
    for (std::size_t index = 0; index < tileset.tiles.size(); ++index)
    {
        const TileData& tile = tileset.tiles[index];
        if (tile.id == 0 || tile.id > maxTileId)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "tile identifiers go from 1 to {}, not {}", maxTileId,
                                   tile.id);
        }
        if (!std::isfinite(tile.fps) || tile.fps < 0.0f)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "tile {} has a negative frame rate", tile.id);
        }
        if (!std::isfinite(tile.probability) || tile.probability < 0.0f)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "tile {} has a negative probability", tile.id);
        }
        const TerrainSetData* const set = tileset.terrainSet(tile.terrainSet);
        if (set == nullptr && tile.terrainSet != noTerrain)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "tile {} names terrain set {}, which does not exist", tile.id,
                                   tile.terrainSet);
        }
        const std::size_t terrains = set != nullptr ? set->terrains.size() : 0;
        if (!inRange(tile.terrain, terrains) ||
            !std::ranges::all_of(tile.terrainBits, [&](std::int32_t bit) { return inRange(bit, terrains); }))
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "tile {} names a terrain its set does not have", tile.id);
        }
        for (std::size_t other = 0; other < index; ++other)
        {
            if (tileset.tiles[other].id == tile.id)
            {
                return core::makeError(core::ErrorCode::InvalidArgument, "two tiles are numbered {}", tile.id);
            }
        }
    }
    return {};
}

} // namespace devex::asset
