#include <devex/scene/TilemapComponents.hpp>

#include <devex/core/Base64.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <optional>

namespace devex::scene {
namespace {

constexpr std::int32_t blockSize = TileGrid::blockSize;
constexpr std::size_t blockCells = static_cast<std::size_t>(blockSize * blockSize);

[[nodiscard]] constexpr std::int32_t floorDivide(std::int32_t value, std::int32_t divisor) noexcept
{
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

// The block holding a cell, and the index of the cell in it, row by row from the bottom.
[[nodiscard]] std::pair<std::pair<std::int32_t, std::int32_t>, std::size_t> locate(math::IVec2 cell) noexcept
{
    const std::int32_t blockX = floorDivide(cell.x, blockSize);
    const std::int32_t blockY = floorDivide(cell.y, blockSize);
    const auto index = static_cast<std::size_t>((cell.y - blockY * blockSize) * blockSize + (cell.x - blockX * blockSize));
    return {{blockX, blockY}, index};
}

[[nodiscard]] std::optional<std::int32_t> parseInteger(std::string_view text)
{
    std::int32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() ? std::optional(value) : std::nullopt;
}

// "x,y:cells": the coordinates of the block and its cells, or nothing when the text is not one.
[[nodiscard]] std::optional<std::pair<std::pair<std::int32_t, std::int32_t>, std::array<std::uint16_t, blockCells>>>
decodeBlock(std::string_view text)
{
    const std::size_t colon = text.find(':');
    const std::size_t comma = text.find(',');
    if (colon == std::string_view::npos || comma == std::string_view::npos || comma > colon)
    {
        return std::nullopt;
    }
    const std::optional<std::int32_t> x = parseInteger(text.substr(0, comma));
    const std::optional<std::int32_t> y = parseInteger(text.substr(comma + 1, colon - comma - 1));
    const std::optional<std::vector<std::byte>> bytes = core::decodeBase64(text.substr(colon + 1));
    if (!x || !y || !bytes || bytes->size() != blockCells * 2)
    {
        return std::nullopt;
    }
    std::array<std::uint16_t, blockCells> cells{};
    for (std::size_t index = 0; index < blockCells; ++index)
    {
        cells[index] = static_cast<std::uint16_t>(std::to_integer<std::uint16_t>((*bytes)[index * 2]) |
                                                  (std::to_integer<std::uint16_t>((*bytes)[index * 2 + 1]) << 8));
    }
    return std::pair{std::pair{*x, *y}, cells};
}

[[nodiscard]] std::string encodeBlock(std::pair<std::int32_t, std::int32_t> key, const std::array<std::uint16_t, blockCells>& cells)
{
    std::array<std::byte, blockCells * 2> bytes{};
    for (std::size_t index = 0; index < blockCells; ++index)
    {
        bytes[index * 2] = static_cast<std::byte>(cells[index] & 0xFF);
        bytes[index * 2 + 1] = static_cast<std::byte>(cells[index] >> 8);
    }
    return std::format("{},{}:{}", key.first, key.second, core::encodeBase64(bytes));
}

[[nodiscard]] bool isEmpty(const std::array<std::uint16_t, blockCells>& cells) noexcept
{
    return std::ranges::all_of(cells, [](std::uint16_t cell) { return cell == 0; });
}

// The world transform with the cells scaled in: cell coordinates to the world.
[[nodiscard]] math::Mat4 cellsToWorld(const Tilemap& tilemap, const math::Mat4& world) noexcept
{
    return world * math::scale(math::Mat4{1.0f}, math::Vec3{tilemap.cellSize.x, tilemap.cellSize.y, 1.0f});
}

} // namespace

DEVEX_REFLECT(Tilemap)
{
    type.field("tileset", &Tilemap::tileset, {.assetType = "tileset"})
        .field("cell_size", &Tilemap::cellSize)
        .field("color", &Tilemap::color, {.color = true})
        .field("sorting_layer", &Tilemap::sortingLayer, {.sortingLayer = true})
        .field("order", &Tilemap::order)
        .field("lit", &Tilemap::lit)
        .field("unshaded", &Tilemap::unshaded)
        .field("light_mask", &Tilemap::lightMask, {.bits = true})
        .field("occluder_mask", &Tilemap::occluderMask, {.bits = true})
        .field("material", &Tilemap::material, {.assetType = "material"})
        .field("blocks", &Tilemap::blocks, {.hidden = true});
}

TileGrid TileGrid::read(const Tilemap& tilemap)
{
    TileGrid grid;
    for (const std::string& text : tilemap.blocks)
    {
        auto block = decodeBlock(text);
        if (!block)
        {
            ++grid.m_skippedBlocks;
            continue;
        }
        grid.m_blocks[{block->first.second, block->first.first}] = block->second;
    }
    return grid;
}

std::uint16_t TileGrid::at(math::IVec2 cell) const noexcept
{
    const auto [key, index] = locate(cell);
    const auto found = m_blocks.find({key.second, key.first});
    return found != m_blocks.end() ? found->second[index] : 0;
}

void TileGrid::set(math::IVec2 cell, std::uint16_t value)
{
    const auto [key, index] = locate(cell);
    const std::pair<std::int32_t, std::int32_t> row{key.second, key.first};
    const auto found = m_blocks.find(row);
    if (found == m_blocks.end())
    {
        if (value != 0)
        {
            m_blocks[row][index] = value;
        }
        return;
    }
    found->second[index] = value;
    if (value == 0 && isEmpty(found->second))
    {
        m_blocks.erase(found);
    }
}

void TileGrid::clear() noexcept
{
    m_blocks.clear();
}

std::vector<TileCell> TileGrid::cells() const
{
    std::vector<TileCell> cells;
    for (const auto& [row, block] : m_blocks)
    {
        for (std::size_t index = 0; index < blockCells; ++index)
        {
            if (block[index] != 0)
            {
                const auto local = static_cast<std::int32_t>(index);
                cells.push_back({.cell = {row.second * blockSize + local % blockSize, row.first * blockSize + local / blockSize},
                                 .value = block[index]});
            }
        }
    }
    return cells;
}

std::size_t TileGrid::count() const noexcept
{
    std::size_t count = 0;
    for (const auto& [row, block] : m_blocks)
    {
        count += static_cast<std::size_t>(std::ranges::count_if(block, [](std::uint16_t cell) { return cell != 0; }));
    }
    return count;
}

std::optional<std::pair<math::IVec2, math::IVec2>> TileGrid::bounds() const
{
    std::optional<std::pair<math::IVec2, math::IVec2>> bounds;
    for (const TileCell& cell : cells())
    {
        if (!bounds)
        {
            bounds = std::pair{cell.cell, cell.cell};
            continue;
        }
        bounds->first = math::min(bounds->first, cell.cell);
        bounds->second = math::max(bounds->second, cell.cell);
    }
    return bounds;
}

std::size_t TileGrid::skippedBlocks() const noexcept
{
    return m_skippedBlocks;
}

void TileGrid::write(Tilemap& tilemap) const
{
    tilemap.blocks.clear();
    for (const auto& [row, block] : m_blocks)
    {
        if (!isEmpty(block))
        {
            tilemap.blocks.push_back(encodeBlock({row.second, row.first}, block));
        }
    }
}

std::uint16_t tileAt(const Tilemap& tilemap, math::IVec2 cell)
{
    const auto [key, index] = locate(cell);
    const std::string prefix = std::format("{},{}:", key.first, key.second);
    for (const std::string& text : tilemap.blocks)
    {
        if (text.starts_with(prefix))
        {
            const auto block = decodeBlock(text);
            return block ? block->second[index] : 0;
        }
    }
    return 0;
}

void setTile(Tilemap& tilemap, math::IVec2 cell, std::uint16_t value)
{
    const auto [key, index] = locate(cell);
    const std::string prefix = std::format("{},{}:", key.first, key.second);
    for (auto text = tilemap.blocks.begin(); text != tilemap.blocks.end(); ++text)
    {
        if (!text->starts_with(prefix))
        {
            continue;
        }
        auto block = decodeBlock(*text);
        std::array<std::uint16_t, blockCells> cells = block ? block->second : std::array<std::uint16_t, blockCells>{};
        cells[index] = value;
        if (isEmpty(cells))
        {
            tilemap.blocks.erase(text);
        }
        else
        {
            *text = encodeBlock(key, cells);
        }
        return;
    }
    if (value != 0)
    {
        // A new block keeps the order TileGrid writes: rows, then columns.
        TileGrid grid = TileGrid::read(tilemap);
        grid.set(cell, value);
        grid.write(tilemap);
    }
}

math::IVec2 cellAt(const Tilemap& tilemap, const math::Mat4& world, math::Vec3 point) noexcept
{
    const math::Vec3 local = math::Vec3(math::inverse(cellsToWorld(tilemap, world)) * math::Vec4(point, 1.0f));
    return {static_cast<std::int32_t>(std::floor(local.x)), static_cast<std::int32_t>(std::floor(local.y))};
}

math::Vec3 cellCenter(const Tilemap& tilemap, const math::Mat4& world, math::IVec2 cell) noexcept
{
    return math::Vec3(cellsToWorld(tilemap, world) *
                      math::Vec4(static_cast<float>(cell.x) + 0.5f, static_cast<float>(cell.y) + 0.5f, 0.0f, 1.0f));
}

} // namespace devex::scene
