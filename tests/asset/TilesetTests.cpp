#include <devex/asset/Artifact.hpp>
#include <devex/asset/TilesetData.hpp>
#include <devex/asset/import/TilesetFile.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

using devex::asset::AssetId;
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
    CHECK(text.starts_with("[tileset format=1]"));
    CHECK(text.find("collision=\"top\"") != std::string::npos);
    CHECK(text.find("data = \"water\"") != std::string::npos);
    const auto parsed = devex::asset::parseTilesetFile(text);
    REQUIRE(parsed.has_value());
    CHECK(*parsed == tileset);

    const auto decoded = devex::asset::decodeTileset(devex::asset::encodeTileset(tileset));
    REQUIRE(decoded.has_value());
    CHECK(*decoded == tileset);

    CHECK_FALSE(devex::asset::parseTilesetFile("[frames format=1]\n").has_value());
    CHECK_FALSE(devex::asset::parseTilesetFile("[tileset format=1]\n\n[tile sprite=asset(\"x\")]\n").has_value());
    CHECK_FALSE(devex::asset::parseTilesetFile("[tileset format=1]\n\n[tile id=1 collision=\"wall\"]\n").has_value());
    CHECK(devex::asset::parseTileCollision("full") == TileCollision::Full);
    CHECK(devex::asset::toString(TileCollision::Top) == "top");
}
