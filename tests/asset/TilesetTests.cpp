#include <devex/asset/Artifact.hpp>
#include <devex/asset/TilesetData.hpp>
#include <devex/asset/import/TilesetFile.hpp>

#include <catch2/catch_test_macros.hpp>

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
    CHECK(text.starts_with("[tileset format=2]"));
    CHECK(text.find("collision=\"top\"") != std::string::npos);
    CHECK(text.find("data = \"water\"") != std::string::npos);
    const auto parsed = devex::asset::parseTilesetFile(text);
    REQUIRE(parsed.has_value());
    CHECK(*parsed == tileset);

    const auto decoded = devex::asset::decodeTileset(devex::asset::encodeTileset(tileset));
    REQUIRE(decoded.has_value());
    CHECK(*decoded == tileset);

    CHECK_FALSE(devex::asset::parseTilesetFile("[frames format=1]\n").has_value());
    CHECK_FALSE(devex::asset::parseTilesetFile("[tileset format=3]\n").has_value());
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
    CHECK(text.starts_with("[tileset format=2]"));
    CHECK(text.find("[terrain_set mode=\"sides\" mirror_x=true mirror_y=false]") != std::string::npos);
    CHECK(text.find("[terrain name=\"Dirt\" color=vec4(0.5, 0.3, 0.1, 1)]") != std::string::npos);
    CHECK(text.find("terrain_set=0 terrain=0]") != std::string::npos);
    CHECK(text.find("bits = list(0, 1, 0, -1, -1, -1, 1, 1)") != std::string::npos);
    CHECK(text.find("probability = 0.25") != std::string::npos);
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
