#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/AssetId.hpp>
#include <devex/math/Math.hpp>
#include <devex/reflection/Reflection.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace devex::scene {

// A cell holds a tile of the tileset in its 14 lowest bits, 0 for none, and mirrors it by the two
// highest ones.
inline constexpr std::uint16_t tileIdMask = 0x3FFF;
inline constexpr std::uint16_t tileFlipX = 0x4000;
inline constexpr std::uint16_t tileFlipY = 0x8000;

[[nodiscard]] constexpr std::uint32_t tileIdOf(std::uint16_t cell) noexcept
{
    return cell & tileIdMask;
}

// A grid of tiles in the XY plane of its entity, painted from a tileset: cell (x, y) covers
// [x, x + 1) by [y, y + 1) cells from the origin, y up. Every tile fills its cell. Tilemaps draw
// among the sprites and the blended surfaces, by sorting layer, order, then distance.
struct DEVEX_API Tilemap
{
    asset::AssetId tileset;
    // The size of a cell, in meters.
    math::Vec2 cellSize{1.0f};
    // Multiplies every tile.
    math::Vec4 color{1.0f};
    std::string sortingLayer = "Default";
    std::int32_t order = 0;
    // Lit as matte surfaces by the sun, the sky and the lights; otherwise shows its colors as they
    // are.
    bool lit = false;
    // Out of reach of the 2D lights and of the CanvasModulate.
    bool unshaded = false;
    // The 2D lights whose item mask shares a bit with it shine on it.
    std::uint32_t lightMask = 1;
    // The tiles that occlude, as their tileset says, hide the 2D lights whose shadow mask shares a
    // bit with it.
    std::uint32_t occluderMask = 1;
    // A material whose canvas_item shader draws the tiles; none draws them as they are.
    asset::AssetId material;
    // The painted cells, 16 by 16 per block, as the scene saves them: "x,y:" the block, then its
    // cells in base64, two bytes each, row by row from the bottom. TileGrid reads and writes them.
    std::vector<std::string> blocks;
    // The values its tiles give the instance uniforms of the shader of its material, by name; the
    // others keep their defaults. Edited under Instance Shader Parameters, or by setInstanceShaderParameter.
    std::vector<std::string> instanceShaderParameters;
    std::vector<math::Vec4> instanceShaderValues;
};
DEVEX_DECLARE_ENGINE_REFLECTION(Tilemap);

struct DEVEX_API TileCell
{
    math::IVec2 cell{0};
    std::uint16_t value = 0;

    bool operator==(const TileCell&) const = default;
};

// The cells of a tilemap decoded, to read and change many of them: read from a Tilemap, changed,
// then written back.
class DEVEX_API TileGrid
{
public:
    static constexpr std::int32_t blockSize = 16;

    TileGrid() = default;
    // Blocks that cannot be read are left out and counted.
    [[nodiscard]] static TileGrid read(const Tilemap& tilemap);

    [[nodiscard]] std::uint16_t at(math::IVec2 cell) const noexcept;
    // 0 empties the cell.
    void set(math::IVec2 cell, std::uint16_t value);
    void clear() noexcept;

    // Every painted cell, block by block.
    [[nodiscard]] std::vector<TileCell> cells() const;
    [[nodiscard]] std::size_t count() const noexcept;
    // The smallest rectangle of cells holding every painted one, both corners inside; nothing
    // when the grid is empty.
    [[nodiscard]] std::optional<std::pair<math::IVec2, math::IVec2>> bounds() const;
    [[nodiscard]] std::size_t skippedBlocks() const noexcept;

    // Replaces the blocks of the tilemap, in order of their rows then columns; empty blocks are
    // left out.
    void write(Tilemap& tilemap) const;

    bool operator==(const TileGrid& other) const noexcept
    {
        return m_blocks == other.m_blocks;
    }

private:
    using Block = std::array<std::uint16_t, static_cast<std::size_t>(blockSize * blockSize)>;
    // By row of blocks, then column, as they are written.
    std::map<std::pair<std::int32_t, std::int32_t>, Block> m_blocks;
    std::size_t m_skippedBlocks = 0;
};

// One cell, without decoding the whole map.
[[nodiscard]] DEVEX_API std::uint16_t tileAt(const Tilemap& tilemap, math::IVec2 cell);
DEVEX_API void setTile(Tilemap& tilemap, math::IVec2 cell, std::uint16_t value);

// The cell under a point of the world, for a tilemap whose entity stands at `world`: the point is
// brought into the plane of the tilemap along its Z axis.
[[nodiscard]] DEVEX_API math::IVec2 cellAt(const Tilemap& tilemap, const math::Mat4& world, math::Vec3 point) noexcept;
// The middle of a cell, in the world.
[[nodiscard]] DEVEX_API math::Vec3 cellCenter(const Tilemap& tilemap, const math::Mat4& world, math::IVec2 cell) noexcept;

} // namespace devex::scene
