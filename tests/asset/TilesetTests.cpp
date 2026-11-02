#include <devex/asset/Artifact.hpp>
#include <devex/asset/TilesetData.hpp>
#include <devex/asset/import/TilesetFile.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <utility>

using devex::asset::AssetId;
using devex::asset::noTerrain;
using devex::asset::TileCollision;
using devex::asset::TilesetData;

namespace {

[[nodiscard]] TilesetData sample()
{
    return TilesetData{.tiles = {{.id = 1, .sprite = AssetId::generate(), .collision = TileCollision::Full},
                                 {.id = 4,
                                  .sprite = AssetId::generate(),
                                  .frames = {AssetId::generate(), AssetId::generate(), AssetId::generate()},
                                  .fps = 4.0f,
                                  .data = "water"},
                                 {.id = 2, .sprite = AssetId::generate(), .collision = TileCollision::Top}}};
}

// Two terrain sets: grass and dirt meeting by their sides and corners, and water by its sides only.
[[nodiscard]] TilesetData withTerrains()
{
    TilesetData tileset = sample();
    tileset.terrainSets = {
        {.mode = devex::asset::TerrainMode::CornersAndSides,
         .terrains = {{.name = "Grass", .color = {0.3f, 0.7f, 0.2f, 1.0f}}, {.name = "Dirt", .color = {0.5f, 0.3f, 0.1f, 1.0f}}}},
        {.mode = devex::asset::TerrainMode::Sides, .mirrorX = true, .terrains = {{.name = "Water", .color = {0.1f, 0.3f, 0.9f, 0.5f}}}},
    };
    tileset.tiles[0].terrainSet = 0;
    tileset.tiles[0].terrain = 0;
    tileset.tiles[0].terrainBits = {0, 1, 0, -1, -1, -1, 1, 1};
    tileset.tiles[0].probability = 0.25f;
    tileset.tiles[1].terrainSet = 1;
    tileset.tiles[1].terrain = 0;
    tileset.tiles[1].terrainBits = {0, -1, -1, -1, 0, -1, -1, -1};
    tileset.tiles[2].occluder = true;
    return tileset;
}

} // namespace

TEST_CASE("A tileset finds its tiles by number, and animated tiles show their frames in turn", "[asset][tileset]")
{
    const TilesetData tileset = sample();
    REQUIRE(tileset.find(4) != nullptr);
    CHECK(tileset.find(3) == nullptr);
    CHECK(tileset.nextId() == 5);
    const devex::asset::TileData& water = *tileset.find(4);
    CHECK(water.spriteAt(0.0) == water.frames[0]);
    CHECK(water.spriteAt(0.3) == water.frames[1]);
    CHECK(water.spriteAt(0.8) == water.frames[0]);
    const devex::asset::TileData& ground = *tileset.find(1);
    CHECK(ground.spriteAt(12.0) == ground.sprite);
    CHECK(devex::asset::validate(tileset).has_value());

    TilesetData twice = tileset;
    twice.tiles.push_back({.id = 1});
    CHECK_FALSE(devex::asset::validate(twice).has_value());
    TilesetData zero = tileset;
    zero.tiles.push_back({.id = 0});
    CHECK_FALSE(devex::asset::validate(zero).has_value());
    TilesetData large = tileset;
    large.tiles.push_back({.id = devex::asset::maxTileId + 1});
    CHECK_FALSE(devex::asset::validate(large).has_value());
}

TEST_CASE("Tilesets survive their files and their cooked form", "[asset][tileset]")
{
    const TilesetData tileset = sample();
    const std::string text = devex::asset::writeTilesetFile(tileset);
    CHECK(text.starts_with("[tileset format=3]"));
    CHECK(text.find("collision=\"top\"") != std::string::npos);
    CHECK(text.find("data = \"water\"") != std::string::npos);
    const auto parsed = devex::asset::parseTilesetFile(text);
    REQUIRE(parsed.has_value());
    CHECK(*parsed == tileset);

    const auto decoded = devex::asset::decodeTileset(devex::asset::encodeTileset(tileset));
    REQUIRE(decoded.has_value());
    CHECK(*decoded == tileset);

    CHECK_FALSE(devex::asset::parseTilesetFile("[frames format=1]\n").has_value());
    CHECK_FALSE(devex::asset::parseTilesetFile("[tileset format=4]\n").has_value());
    CHECK_FALSE(devex::asset::parseTilesetFile("[tileset format=1]\n\n[tile sprite=asset(\"x\")]\n").has_value());
    CHECK_FALSE(devex::asset::parseTilesetFile("[tileset format=1]\n\n[tile id=1 collision=\"wall\"]\n").has_value());
    CHECK(devex::asset::parseTileCollision("full") == TileCollision::Full);
    CHECK(devex::asset::toString(TileCollision::Top) == "top");
}

TEST_CASE("Tilesets keep their terrain sets and the terrains of their tiles", "[asset][tileset][terrain]")
{
    const TilesetData tileset = withTerrains();
    REQUIRE(devex::asset::validate(tileset).has_value());
    const std::string text = devex::asset::writeTilesetFile(tileset);
    CHECK(text.starts_with("[tileset format=3]"));
    CHECK(text.find("[terrain_set mode=\"sides\" mirror_x=true mirror_y=false]") != std::string::npos);
    CHECK(text.find("[terrain name=\"Dirt\" color=vec4(0.5, 0.3, 0.1, 1)]") != std::string::npos);
    CHECK(text.find("terrain_set=0 terrain=0]") != std::string::npos);
    CHECK(text.find("bits = list(0, 1, 0, -1, -1, -1, 1, 1)") != std::string::npos);
    CHECK(text.find("probability = 0.25") != std::string::npos);
    CHECK(text.find("collision=\"top\" occluder=true]") != std::string::npos);
    const auto parsed = devex::asset::parseTilesetFile(text);
    REQUIRE(parsed.has_value());
    CHECK(*parsed == tileset);
    const auto decoded = devex::asset::decodeTileset(devex::asset::encodeTileset(tileset));
    REQUIRE(decoded.has_value());
    CHECK(*decoded == tileset);

    // Files of the first format, without terrains, still read.
    const auto old = devex::asset::parseTilesetFile("[tileset format=1]\n\n[tile id=3 collision=\"full\"]\n");
    REQUIRE(old.has_value());
    CHECK(old->terrainSets.empty());
    CHECK(old->tiles.front().terrainSet == noTerrain);
    CHECK(old->tiles.front().probability == 1.0f);

    // Bits the mode does not read are left out, and terrains a set does not have refused.
    const auto corners = devex::asset::parseTilesetFile("[tileset format=2]\n\n[terrain_set mode=\"corners\"]\n\n[terrain name=\"Rock\"]\n\n"
                                                        "[tile id=1 terrain_set=0 terrain=0]\nbits = list(0, 0, 0, 0, 0, 0, 0, 0)\n");
    REQUIRE(corners.has_value());
    CHECK(corners->tiles.front().terrainBits == devex::asset::TerrainBits{-1, 0, -1, 0, -1, 0, -1, 0});
    CHECK_FALSE(devex::asset::parseTilesetFile("[tileset format=2]\n\n[terrain name=\"Rock\"]\n").has_value());
    CHECK_FALSE(devex::asset::parseTilesetFile("[tileset format=2]\n\n[terrain_set mode=\"hex\"]\n").has_value());
    CHECK_FALSE(devex::asset::parseTilesetFile("[tileset format=2]\n\n[terrain_set]\n\n[tile id=1 terrain_set=0 terrain=2]\n").has_value());
    CHECK_FALSE(devex::asset::parseTilesetFile("[tileset format=2]\n\n[tile id=1 terrain_set=1 terrain=0]\n").has_value());
    CHECK_FALSE(devex::asset::parseTilesetFile("[tileset format=2]\n\n[terrain_set]\n\n[tile id=1 terrain_set=0]\nbits = list(0)\n")
                     .has_value());
    CHECK(devex::asset::parseTerrainMode("corners_and_sides") == devex::asset::TerrainMode::CornersAndSides);
    CHECK(devex::asset::toString(devex::asset::TerrainMode::Corners) == "corners");

    TilesetData negative = tileset;
    negative.tiles[2].probability = -1.0f;
    CHECK_FALSE(devex::asset::validate(negative).has_value());
}

TEST_CASE("Removing a terrain or a terrain set keeps what the tiles name of the others", "[asset][tileset][terrain]")
{
    TilesetData tileset = withTerrains();
    CHECK(tileset.findTerrain("Dirt") == std::pair{0, 1});
    CHECK(tileset.findTerrain("Water") == std::pair{1, 0});
    CHECK_FALSE(tileset.findTerrain("Lava").has_value());
    REQUIRE(tileset.terrain(1, 0) != nullptr);
    CHECK(tileset.terrain(1, 0)->name == "Water");
    CHECK(tileset.terrain(1, 1) == nullptr);
    CHECK(tileset.terrainSet(2) == nullptr);

    tileset.removeTerrain(0, 0);
    CHECK(tileset.terrainSets[0].terrains.size() == 1);
    CHECK(tileset.tiles[0].terrain == noTerrain);
    CHECK(tileset.tiles[0].terrainBits == devex::asset::TerrainBits{-1, 0, -1, -1, -1, -1, 0, 0});
    CHECK(tileset.tiles[1].terrainBits[0] == 0);
    CHECK(devex::asset::validate(tileset).has_value());

    tileset.removeTerrainSet(0);
    CHECK(tileset.terrainSets.size() == 1);
    CHECK(tileset.tiles[0].terrainSet == noTerrain);
    CHECK(tileset.tiles[0].terrainBits == devex::asset::noTerrainBits);
    CHECK(tileset.tiles[1].terrainSet == 0);
    CHECK(tileset.findTerrain("Water") == std::pair{0, 0});
    CHECK(devex::asset::validate(tileset).has_value());
}

TEST_CASE("Custom tile collision outlines survive source files and artifacts",
          "[asset][tileset][tilecollision]")
{
    auto tileset = sample();
    auto& tile = tileset.tiles.front();
    tile.collision = TileCollision::Polygon;
    tile.collisionPoints = {{0, 0}, {1, 0}, {1, 0.4f}, {0.4f, 0.4f}, {0.4f, 1}, {0, 1}};
    tile.collisionOneWay = true;
    REQUIRE(devex::asset::validate(tileset));
    const auto text = devex::asset::writeTilesetFile(tileset);
    CHECK(text.find("collision_points = list(vec2(") != std::string::npos);
    CHECK(text.find("collision_one_way=true") != std::string::npos);
    const auto source = devex::asset::parseTilesetFile(text);
    REQUIRE(source);
    CHECK(*source == tileset);
    const auto cooked = devex::asset::decodeTileset(devex::asset::encodeTileset(tileset));
    REQUIRE(cooked);
    CHECK(*cooked == tileset);
    // Temporarily disabling the collision keeps the authored outline.
    tile.collision = TileCollision::None;
    const auto disabled = devex::asset::parseTilesetFile(devex::asset::writeTilesetFile(tileset));
    REQUIRE(disabled);
    CHECK(*disabled == tileset);
    CHECK(devex::asset::parseTileCollision("polygon") == TileCollision::Polygon);
    CHECK_FALSE(
        devex::asset::parseTilesetFile("[tileset format=3]\n[tile id=1 collision=\"polygon\"]\n"));
    CHECK_FALSE(devex::asset::parseTilesetFile(
        "[tileset format=3]\n[tile id=1]\ncollision_points = list(vec3(0,0,0))\n"));
}

TEST_CASE("Tile collision outlines reject crossed, degenerate and unbounded shapes",
          "[asset][tileset][tilecollision]")
{
    using devex::math::Vec2;
    const std::vector<std::vector<Vec2>> invalid{
        {},
        {{0, 0}, {1, 1}},
        {{0, 0}, {1, 0}, {0.5f, 0}},
        {{0, 0}, {1, 1}, {0, 1}, {1, 0}},
        {{0, 0}, {1, 0}, {1, 0}, {0, 1}},
        {{0, 0}, {1, 0}, {0.5f, 0}, {0.5f, 1}, {0, 1}},
        {{0, 0}, {2, 0}, {0, 1}},
        {{0, 0}, {1, 0}, {0, -0.1f}},
        {{0, 0}, {1, 0}, {0, std::numeric_limits<float>::quiet_NaN()}},
        {{0, 0}, {1, 0}, {0, std::numeric_limits<float>::infinity()}},
        std::vector<Vec2>(65, {0, 0})};
    for (std::size_t i = 0; i < invalid.size(); ++i)
    {
        CAPTURE(i);
        CHECK_FALSE(devex::asset::validateTileCollision(invalid[i]));
        CHECK_FALSE(devex::asset::decomposeTileCollision(invalid[i]));
    }
    CHECK(devex::asset::validateTileCollision(
        std::vector<Vec2>{{0, 0}, {0.5f, 0}, {1, 0}, {1, 1}, {0, 1}}));
}

TEST_CASE("Concave tile collisions decompose without filling their empty regions",
          "[asset][tileset][tilecollision]")
{
    using devex::math::Vec2;
    std::vector<Vec2> outline{{0, 0}, {1, 0}, {1, 0.4f}, {0.4f, 0.4f}, {0.4f, 1}, {0, 1}};
    const auto cross = [](Vec2 a, Vec2 b, Vec2 c)
    { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
    for (int winding = 0; winding < 2; ++winding)
    {
        const auto pieces = devex::asset::decomposeTileCollision(outline);
        REQUIRE(pieces);
        double area = 0;
        for (const auto& piece : *pieces)
        {
            CHECK(piece.size() >= 3);
            CHECK(piece.size() <= 8);
            for (std::size_t i = 0; i < piece.size(); ++i)
            {
                CHECK(cross(piece[i], piece[(i + 1) % piece.size()],
                            piece[(i + 2) % piece.size()]) > 0);
                area += cross({}, piece[i], piece[(i + 1) % piece.size()]) * 0.5;
            }
        }
        CHECK(area == Catch::Approx(0.64).margin(0.00001));
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < 12; ++x)
            {
                const Vec2 p{(static_cast<float>(x) + 0.5f) / 12,
                             (static_cast<float>(y) + 0.5f) / 12};
                const bool covered = std::ranges::any_of(
                    *pieces,
                    [&](const auto& piece)
                    {
                        for (std::size_t i = 0; i < piece.size(); ++i)
                            if (cross(piece[i], piece[(i + 1) % piece.size()], p) < -0.00001f)
                                return false;
                        return true;
                    });
                CHECK(covered == (p.x < 0.4f || p.y < 0.4f));
            }
        std::ranges::reverse(outline);
    }
    std::vector<Vec2> circle;
    for (int i = 0; i < 32; ++i)
    {
        const float angle = static_cast<float>(i) * 2 * std::numbers::pi_v<float> / 32;
        circle.push_back({0.5f + 0.5f * std::cos(angle), 0.5f + 0.5f * std::sin(angle)});
    }
    const auto round = devex::asset::decomposeTileCollision(circle);
    REQUIRE(round);
    CHECK(round->size() > 1);
    for (const auto& part : *round)
        CHECK(part.size() <= 8);
}
