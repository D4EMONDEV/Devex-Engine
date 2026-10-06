#include <devex/scene/TileTerrain.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <random>
#include <set>
#include <utility>
#include <vector>

using devex::asset::noTerrain;
using devex::asset::TerrainMode;
using devex::asset::TileData;
using devex::asset::TilesetData;
using devex::math::IVec2;
using devex::scene::TerrainPaint;
using devex::scene::TerrainPattern;
using devex::scene::TileGrid;

namespace {

constexpr std::size_t right = 0;
constexpr std::size_t topRight = 1;
constexpr std::size_t top = 2;
constexpr std::size_t left = 4;
constexpr std::size_t bottom = 6;

[[nodiscard]] std::vector<std::size_t> matched(TerrainMode mode)
{
    std::vector<std::size_t> bits;
    for (std::size_t index = 0; index < devex::asset::tileNeighborCount; ++index)
    {
        if (devex::asset::matchesNeighbor(mode, static_cast<devex::asset::TileNeighbor>(index)))
        {
            bits.push_back(index);
        }
    }
    return bits;
}

// A tile for every pattern of a terrain in the mode, or only those a mirror does not make.
[[nodiscard]] TilesetData completeTileset(TerrainMode mode, bool mirrorX = false)
{
    TilesetData tileset;
    tileset.terrainSets.push_back({.mode = mode, .mirrorX = mirrorX, .terrains = {{.name = "Ground"}}});
    const std::vector<std::size_t> bits = matched(mode);
    for (std::uint32_t mask = 0; mask < (1u << bits.size()); ++mask)
    {
        TileData tile{.id = mask + 1, .terrainSet = 0, .terrain = 0};
        for (std::size_t index = 0; index < bits.size(); ++index)
        {
            tile.terrainBits[bits[index]] = (mask & (1u << index)) != 0 ? 0 : noTerrain;
        }
        // Mirrored across, a tile open on the left and closed on the right gives the other one.
        if (mirrorX && tile.terrainBits[left] == 0 && tile.terrainBits[right] == noTerrain)
        {
            continue;
        }
        tileset.tiles.push_back(tile);
    }
    return tileset;
}

// What a cell of a terrain should show: joined to the cells of it across its sides, and at a corner
// when the three other cells there are of it too.
[[nodiscard]] TerrainPattern expected(const std::set<std::pair<int, int>>& ground, IVec2 cell, TerrainMode mode)
{
    const auto has = [&](IVec2 at) { return ground.contains({at.x, at.y}); };
    TerrainPattern pattern{.terrain = 0};
    for (const std::size_t bit : matched(mode))
    {
        const IVec2 offset = devex::asset::neighborOffset(static_cast<devex::asset::TileNeighbor>(bit));
        const bool joined = bit % 2 == 0 ? has(cell + offset)
                                         : has(cell + offset) && has(cell + IVec2{offset.x, 0}) && has(cell + IVec2{0, offset.y});
        pattern.bits[bit] = joined ? 0 : noTerrain;
    }
    return pattern;
}

[[nodiscard]] TerrainPattern shown(const TilesetData& tileset, const TileGrid& grid, IVec2 cell)
{
    return devex::scene::terrainPattern(tileset, 0, grid.at(cell)).value_or(TerrainPattern{.terrain = 99});
}

} // namespace

TEST_CASE("Terrain brushes keep every tile matching its neighbours, however they paint and erase", "[scene][tilemap][terrain]")
{
    const auto [mode, mirror] = GENERATE(std::pair{TerrainMode::CornersAndSides, false}, std::pair{TerrainMode::Sides, false},
                                         std::pair{TerrainMode::Corners, false}, std::pair{TerrainMode::Sides, true});
    CAPTURE(mode, mirror);
    const TilesetData tileset = completeTileset(mode, mirror);
    REQUIRE(devex::asset::validate(tileset).has_value());
    TileGrid grid;
    std::set<std::pair<int, int>> ground;
    std::mt19937 random(7);
    std::uniform_int_distribution<int> coordinate(0, 9);
    std::uniform_int_distribution<int> kind(0, 9);
    bool mirrored = false;
    for (int step = 0; step < 200; ++step)
    {
        // Single cells, strokes and rectangles, painted or erased.
        std::vector<IVec2> cells{{coordinate(random), coordinate(random)}};
        const int what = kind(random);
        if (what >= 6)
        {
            const IVec2 corner{coordinate(random), coordinate(random)};
            for (int y = std::min(cells[0].y, corner.y); y <= std::max(cells[0].y, corner.y); ++y)
            {
                for (int x = std::min(cells[0].x, corner.x); x <= std::max(cells[0].x, corner.x); ++x)
                {
                    cells.push_back({x, y});
                }
            }
        }
        const bool erase = what % 3 == 0;
        static_cast<void>(devex::scene::paintTerrain(grid, tileset, cells, 0, erase ? noTerrain : 0));
        for (const IVec2 cell : cells)
        {
            if (erase)
            {
                ground.erase({cell.x, cell.y});
            }
            else
            {
                ground.insert({cell.x, cell.y});
            }
        }
        for (int y = -1; y <= 10; ++y)
        {
            for (int x = -1; x <= 10; ++x)
            {
                CAPTURE(step, x, y);
                if (ground.contains({x, y}))
                {
                    REQUIRE(shown(tileset, grid, {x, y}) == expected(ground, {x, y}, mode));
                    mirrored = mirrored || (grid.at({x, y}) & devex::scene::tileFlipX) != 0;
                }
                else
                {
                    REQUIRE(grid.at({x, y}) == 0);
                }
            }
        }
    }
    CHECK(mirrored == mirror);
}

TEST_CASE("Terrains painted into another one meet it with the tiles that name both", "[scene][tilemap][terrain]")
{
    // Grass and dirt, with a tile for any terrain on any side.
    TilesetData tileset;
    tileset.terrainSets.push_back({.mode = TerrainMode::Sides, .terrains = {{.name = "Grass"}, {.name = "Dirt"}}});
    std::uint32_t id = 1;
    for (std::int32_t middle = 0; middle < 2; ++middle)
    {
        for (int mask = 0; mask < 81; ++mask)
        {
            TileData tile{.id = id++, .terrainSet = 0, .terrain = middle};
            int rest = mask;
            for (const std::size_t side : {right, top, left, bottom})
            {
                tile.terrainBits[side] = rest % 3 - 1;
                rest /= 3;
            }
            tileset.tiles.push_back(tile);
        }
    }
    REQUIRE(devex::asset::validate(tileset).has_value());
    TileGrid grid;
    std::vector<IVec2> field;
    for (int y = 0; y < 5; ++y)
    {
        for (int x = 0; x < 5; ++x)
        {
            field.push_back({x, y});
        }
    }
    devex::scene::paintTerrain(grid, tileset, field, 0, 0);
    const TerrainPattern grass{.terrain = 0, .bits = {0, -1, 0, -1, 0, -1, 0, -1}};
    CHECK(shown(tileset, grid, {2, 2}) == grass);

    // A patch of dirt in the grass has grass all around it; the grass is left as it is.
    const std::vector<IVec2> patch{{2, 2}};
    CHECK(devex::scene::paintTerrain(grid, tileset, patch, 0, 1) == patch);
    CHECK(shown(tileset, grid, {2, 2}) == TerrainPattern{.terrain = 1, .bits = {0, -1, 0, -1, 0, -1, 0, -1}});
    // More dirt joins it, and the first patch opens towards it.
    const std::vector<IVec2> more{{3, 2}};
    const std::vector<IVec2> changed = devex::scene::paintTerrain(grid, tileset, more, 0, 1);
    CHECK(changed.size() == 2);
    CHECK(shown(tileset, grid, {2, 2}) == TerrainPattern{.terrain = 1, .bits = {1, -1, 0, -1, 0, -1, 0, -1}});
    CHECK(shown(tileset, grid, {3, 2}) == TerrainPattern{.terrain = 1, .bits = {0, -1, 0, -1, 1, -1, 0, -1}});
    CHECK(shown(tileset, grid, {1, 2}) == grass);
    CHECK(shown(tileset, grid, {4, 2}).bits[left] == 0);
    CHECK(devex::scene::terrainOf(tileset, grid.at({3, 2})) == std::pair{0, 1});
    CHECK(devex::scene::terrainOf(tileset, 0) == std::pair{noTerrain, noTerrain});
}

TEST_CASE("Terrain paths join each cell only to the one before and the one after it", "[scene][tilemap][terrain]")
{
    const TilesetData tileset = completeTileset(TerrainMode::Sides);
    TileGrid grid;
    const std::vector<IVec2> road{{0, 0}, {1, 0}, {2, 0}, {2, 1}};
    devex::scene::paintTerrain(grid, tileset, road, 0, 0, TerrainPaint::Path);
    CHECK(shown(tileset, grid, {0, 0}).bits == devex::asset::TerrainBits{0, -1, -1, -1, -1, -1, -1, -1});
    CHECK(shown(tileset, grid, {1, 0}).bits == devex::asset::TerrainBits{0, -1, -1, -1, 0, -1, -1, -1});
    CHECK(shown(tileset, grid, {2, 0}).bits == devex::asset::TerrainBits{-1, -1, 0, -1, 0, -1, -1, -1});
    CHECK(shown(tileset, grid, {2, 1}).bits == devex::asset::TerrainBits{-1, -1, -1, -1, -1, -1, 0, -1});

    // Another road along the first stays apart from it; connecting joins them.
    const std::vector<IVec2> along{{0, 1}, {1, 1}};
    devex::scene::paintTerrain(grid, tileset, along, 0, 0, TerrainPaint::Path);
    CHECK(shown(tileset, grid, {0, 1}).bits[bottom] == noTerrain);
    CHECK(shown(tileset, grid, {1, 1}).bits[right] == noTerrain);
    CHECK(shown(tileset, grid, {1, 0}).bits[top] == noTerrain);
    devex::scene::paintTerrain(grid, tileset, along, 0, 0, TerrainPaint::Connect);
    CHECK(shown(tileset, grid, {0, 1}).bits[bottom] == 0);
    CHECK(shown(tileset, grid, {1, 1}).bits[right] == 0);
    CHECK(shown(tileset, grid, {1, 0}).bits[top] == 0);
    CHECK(shown(tileset, grid, {2, 1}).bits[left] == 0);
}

TEST_CASE("Terrain brushes leave other tiles alone, draw variants by probability, and need tiles", "[scene][tilemap][terrain]")
{
    TilesetData tileset = completeTileset(TerrainMode::Sides);
    // A crate outside the terrains, and a second middle tile that is chosen as often as the first.
    tileset.tiles.push_back({.id = 100});
    const std::uint32_t middle = 1 + 0b1111;
    TileData other = *tileset.find(middle);
    other.id = 101;
    tileset.tiles.push_back(other);
    tileset.terrainSets.front().terrains.push_back({.name = "Nothing yet"});

    TileGrid grid;
    grid.set({3, 3}, 100);
    std::vector<IVec2> field;
    for (int y = 0; y < 7; ++y)
    {
        for (int x = 0; x < 7; ++x)
        {
            if (x != 3 || y != 3)
            {
                field.push_back({x, y});
            }
        }
    }
    devex::scene::paintTerrain(grid, tileset, field, 0, 0);
    CHECK(grid.at({3, 3}) == 100);
    // The crate is not ground: the cells beside it close towards it.
    CHECK(shown(tileset, grid, {2, 3}).bits[right] == noTerrain);
    CHECK(shown(tileset, grid, {3, 4}).bits[bottom] == noTerrain);
    std::set<std::uint16_t> middles;
    for (int y = 1; y < 6; ++y)
    {
        middles.insert(grid.at({1, y}));
        middles.insert(grid.at({5, y}));
    }
    CHECK(middles == std::set<std::uint16_t>{static_cast<std::uint16_t>(middle), 101});

    // Painting again changes nothing, and a terrain without tiles paints nothing.
    const TileGrid painted = grid;
    CHECK(devex::scene::paintTerrain(grid, tileset, field, 0, 0).empty());
    CHECK(grid == painted);
    const std::vector<IVec2> one{{9, 9}};
    CHECK(devex::scene::paintTerrain(grid, tileset, one, 0, 1).empty());
    CHECK(devex::scene::paintTerrain(grid, tileset, one, 4, 0).empty());
    CHECK(grid == painted);

    // A tile that is never chosen leaves the other one.
    tileset.tiles.back().probability = 0.0f;
    TileGrid single;
    devex::scene::paintTerrain(single, tileset, field, 0, 0);
    for (int y = 1; y < 6; ++y)
    {
        CHECK(single.at({1, y}) == middle);
    }
    // Erasing with the terrain brush also takes the tiles of no terrain.
    const std::vector<IVec2> crate{{3, 3}};
    devex::scene::paintTerrain(single, tileset, crate, 0, noTerrain);
    CHECK(single.at({3, 3}) == 0);
}
