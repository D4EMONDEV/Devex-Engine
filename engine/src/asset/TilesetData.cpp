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

core::Result<void> validate(const TilesetData& tileset)
{
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
