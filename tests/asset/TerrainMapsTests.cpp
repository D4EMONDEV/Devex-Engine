#include <devex/asset/Artifact.hpp>
#include <devex/asset/TerrainMaps.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numeric>

using Catch::Matchers::WithinAbs;
using devex::asset::TerrainBrush;
using devex::asset::TerrainMaps;
using devex::asset::TerrainRegion;
using devex::asset::TerrainTool;
using devex::math::Vec3;

namespace {

// A terrain of 64 m on 65 samples: one per meter.
constexpr float side = 64.0f;

[[nodiscard]] float heightAt(const TerrainMaps& maps, std::uint32_t x, std::uint32_t z)
{
    return maps.heights[std::size_t{z} * maps.resolution + x];
}

[[nodiscard]] unsigned paintedTotal(const TerrainMaps& maps, std::uint32_t x, std::uint32_t z)
{
    const std::size_t first = (std::size_t{z} * maps.resolution + x) * devex::asset::maxTerrainLayers;
    return std::accumulate(maps.paint.begin() + static_cast<std::ptrdiff_t>(first),
                           maps.paint.begin() + static_cast<std::ptrdiff_t>(first + devex::asset::maxTerrainLayers), 0u);
}

} // namespace

TEST_CASE("Terrain maps check their resolution and come back from their file as they were", "[asset][terrain]")
{
    TerrainMaps maps = devex::asset::makeFlatTerrain(65, 2.0f);
    REQUIRE(devex::asset::validate(maps).has_value());
    CHECK(maps.heights.size() == 65 * 65);
    CHECK(maps.paint.size() == 65 * 65 * 8);
    maps.heights[100] = 7.5f;
    maps.paint[17] = 200;
    maps.holes[3] = 255;
    maps.grass[64] = 90;
    const devex::core::Result<TerrainMaps> decoded = devex::asset::decodeTerrain(devex::asset::encodeTerrain(maps));
    REQUIRE(decoded.has_value());
    CHECK(*decoded == maps);

    // A power of two plus one samples on a side, and every map of the same size.
    CHECK_FALSE(devex::asset::validate(devex::asset::makeFlatTerrain(64)).has_value());
    CHECK_FALSE(devex::asset::validate(devex::asset::makeFlatTerrain(17)).has_value());
    TerrainMaps broken = devex::asset::makeFlatTerrain(33);
    broken.grass.pop_back();
    CHECK_FALSE(devex::asset::validate(broken).has_value());
    broken = devex::asset::makeFlatTerrain(33);
    broken.heights[5] = std::nanf("");
    CHECK_FALSE(devex::asset::validate(broken).has_value());
}

TEST_CASE("The ground of a terrain is read between its samples, with its slope and its holes", "[asset][terrain]")
{
    // A ramp along X: one meter of height per meter.
    TerrainMaps maps = devex::asset::makeFlatTerrain(65);
    for (std::uint32_t z = 0; z < 65; ++z)
    {
        for (std::uint32_t x = 0; x < 65; ++x)
        {
            maps.heights[std::size_t{z} * 65 + x] = static_cast<float>(x);
        }
    }
    CHECK_THAT(devex::asset::terrainHeight(maps, side, 10.0f, 20.0f), WithinAbs(10.0, 1e-4));
    CHECK_THAT(devex::asset::terrainHeight(maps, side, 10.5f, 3.25f), WithinAbs(10.5, 1e-4));
    // Twice as large, the same samples stand twice as far apart.
    CHECK_THAT(devex::asset::terrainHeight(maps, side * 2.0f, 21.0f, 0.0f), WithinAbs(10.5, 1e-4));
    // The edges continue outside.
    CHECK_THAT(devex::asset::terrainHeight(maps, side, -5.0f, 0.0f), WithinAbs(0.0, 1e-4));
    CHECK_THAT(devex::asset::terrainHeight(maps, side, 80.0f, 0.0f), WithinAbs(64.0, 1e-4));
    const Vec3 normal = devex::asset::terrainNormal(maps, side, 30.0f, 30.0f);
    CHECK_THAT(normal.x, WithinAbs(-std::sqrt(0.5), 1e-4));
    CHECK_THAT(normal.y, WithinAbs(std::sqrt(0.5), 1e-4));
    CHECK_THAT(normal.z, WithinAbs(0.0, 1e-4));

    // A ray straight down meets the ramp at its height; through a hole it goes on.
    const std::optional<float> hit = devex::asset::raycastTerrain(maps, side, {12.25f, 50.0f, 8.0f}, {0.0f, -1.0f, 0.0f}, 100.0f);
    REQUIRE(hit.has_value());
    CHECK_THAT(*hit, WithinAbs(50.0 - 12.25, 0.01));
    CHECK_FALSE(devex::asset::raycastTerrain(maps, side, {12.0f, 50.0f, 8.0f}, {0.0f, 1.0f, 0.0f}, 100.0f).has_value());
    CHECK_FALSE(devex::asset::raycastTerrain(maps, side, {-30.0f, 50.0f, 8.0f}, {0.0f, -1.0f, 0.0f}, 100.0f).has_value());
    maps.holes[std::size_t{8} * 65 + 12] = 255;
    CHECK(devex::asset::isTerrainHole(maps, side, 12.2f, 7.9f));
    CHECK_FALSE(devex::asset::raycastTerrain(maps, side, {12.0f, 50.0f, 8.0f}, {0.0f, -1.0f, 0.0f}, 100.0f).has_value());

    // The heights of its blocks, row after row.
    const std::vector<devex::math::Vec2> ranges = devex::asset::terrainHeightRanges(maps, 32);
    REQUIRE(ranges.size() == 4);
    CHECK(ranges[0] == devex::math::Vec2{0.0f, 32.0f});
    CHECK(ranges[1] == devex::math::Vec2{32.0f, 64.0f});
    CHECK(ranges[2] == ranges[0]);
}

TEST_CASE("Brushes sculpt the ground under them, the most at their center", "[asset][terrain]")
{
    TerrainMaps maps = devex::asset::makeFlatTerrain(65);
    const TerrainRegion region = devex::asset::applyTerrainBrush(
        maps, side, TerrainBrush{.tool = TerrainTool::Raise, .center = {32.0f, 32.0f}, .radius = 4.0f, .softness = 1.0f, .amount = 2.0f});
    CHECK(region == TerrainRegion{.x = 29, .z = 29, .width = 7, .height = 7});
    CHECK_THAT(heightAt(maps, 32, 32), WithinAbs(2.0, 1e-4));
    CHECK(heightAt(maps, 34, 32) > 0.0f);
    CHECK(heightAt(maps, 34, 32) < heightAt(maps, 33, 32));
    CHECK(heightAt(maps, 36, 32) == 0.0f);
    CHECK(heightAt(maps, 20, 20) == 0.0f);

    // Smoothing evens the bump out; flattening brings it to a height.
    const float peak = heightAt(maps, 32, 32);
    devex::asset::applyTerrainBrush(maps, side, TerrainBrush{.tool = TerrainTool::Smooth, .center = {32.0f, 32.0f}, .radius = 6.0f, .amount = 1.0f});
    CHECK(heightAt(maps, 32, 32) < peak);
    CHECK(heightAt(maps, 30, 32) > 0.0f);
    devex::asset::applyTerrainBrush(maps, side,
                                    TerrainBrush{.tool = TerrainTool::Flatten, .center = {32.0f, 32.0f}, .radius = 8.0f, .softness = 0.0f, .amount = 1.0f, .height = -1.0f});
    CHECK_THAT(heightAt(maps, 32, 32), WithinAbs(-1.0, 1e-4));
    CHECK_THAT(heightAt(maps, 29, 33), WithinAbs(-1.0, 1e-4));

    // A brush off the terrain changes nothing.
    CHECK(devex::asset::applyTerrainBrush(maps, side, TerrainBrush{.tool = TerrainTool::Raise, .center = {200.0f, 200.0f}}).empty());
}

TEST_CASE("Brushes paint layers that give way to each other, holes and grass", "[asset][terrain]")
{
    TerrainMaps maps = devex::asset::makeFlatTerrain(65);
    const std::size_t sample = (std::size_t{10} * 65 + 10) * devex::asset::maxTerrainLayers;
    const TerrainBrush paint{.tool = TerrainTool::Paint, .center = {10.0f, 10.0f}, .radius = 2.0f, .softness = 0.0f, .amount = 1.0f, .layer = 2};
    devex::asset::applyTerrainBrush(maps, side, paint);
    CHECK(maps.paint[sample + 2] == 255);
    CHECK(paintedTotal(maps, 10, 10) == 255);
    // Another layer takes over half of it, the first giving way.
    TerrainBrush second = paint;
    second.layer = 5;
    second.amount = 0.5f;
    devex::asset::applyTerrainBrush(maps, side, second);
    CHECK(maps.paint[sample + 5] == 128);
    CHECK(maps.paint[sample + 2] == 127);
    CHECK(paintedTotal(maps, 10, 10) <= 255);
    // Taking a layer away leaves the rest to the rules.
    TerrainBrush erase = second;
    erase.erase = true;
    erase.amount = 1.0f;
    devex::asset::applyTerrainBrush(maps, side, erase);
    CHECK(maps.paint[sample + 5] == 0);
    CHECK(maps.paint[sample + 2] == 127);

    devex::asset::applyTerrainBrush(maps, side, TerrainBrush{.tool = TerrainTool::Hole, .center = {40.0f, 40.0f}, .radius = 1.5f});
    CHECK(devex::asset::isTerrainHole(maps, side, 40.0f, 40.0f));
    CHECK_FALSE(devex::asset::isTerrainHole(maps, side, 45.0f, 40.0f));
    devex::asset::applyTerrainBrush(maps, side, TerrainBrush{.tool = TerrainTool::Hole, .center = {40.0f, 40.0f}, .radius = 1.5f, .erase = true});
    CHECK_FALSE(devex::asset::isTerrainHole(maps, side, 40.0f, 40.0f));

    devex::asset::applyTerrainBrush(maps, side, TerrainBrush{.tool = TerrainTool::Grass, .center = {20.0f, 50.0f}, .radius = 3.0f, .amount = 0.5f});
    CHECK(maps.grass[std::size_t{50} * 65 + 20] == 128);
}

TEST_CASE("A copy of a region of the maps puts them back as they were", "[asset][terrain]")
{
    TerrainMaps maps = devex::asset::makeFlatTerrain(33);
    const TerrainMaps original = maps;
    const TerrainBrush brush{.tool = TerrainTool::Noise, .center = {16.0f, 16.0f}, .radius = 5.0f, .amount = 3.0f};
    const TerrainRegion region = devex::asset::applyTerrainBrush(maps, 32.0f, brush);
    REQUIRE_FALSE(region.empty());
    CHECK(maps != original);
    const devex::asset::TerrainPatch after = devex::asset::copyTerrainRegion(maps, region);
    devex::asset::pasteTerrainPatch(maps, devex::asset::copyTerrainRegion(original, region));
    CHECK(maps == original);
    devex::asset::pasteTerrainPatch(maps, after);
    CHECK(maps != original);
    CHECK(region.merged({.x = 0, .z = 30, .width = 2, .height = 3}) ==
          TerrainRegion{.x = 0, .z = region.z, .width = region.x + region.width, .height = 33 - region.z});
}
