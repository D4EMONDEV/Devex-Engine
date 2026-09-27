#include <devex/scene/Components.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/scene/TilemapComponents.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <string>

using Catch::Matchers::WithinAbs;
using devex::math::IVec2;
using devex::scene::TileGrid;
using devex::scene::Tilemap;

TEST_CASE("A tile grid keeps its cells in blocks, negative ones included", "[scene][tilemap]")
{
    TileGrid grid;
    grid.set({0, 0}, 1);
    grid.set({15, 15}, 2);
    grid.set({16, 0}, 3);
    grid.set({-1, -1}, 4 | devex::scene::tileFlipX);
    grid.set({-17, 3}, 5);
    CHECK(grid.at({0, 0}) == 1);
    CHECK(grid.at({15, 15}) == 2);
    CHECK(grid.at({16, 0}) == 3);
    CHECK(devex::scene::tileIdOf(grid.at({-1, -1})) == 4);
    CHECK((grid.at({-1, -1}) & devex::scene::tileFlipX) != 0);
    CHECK(grid.at({1, 0}) == 0);
    CHECK(grid.count() == 5);
    const auto bounds = grid.bounds();
    REQUIRE(bounds.has_value());
    CHECK(bounds->first == IVec2{-17, -1});
    CHECK(bounds->second == IVec2{16, 15});

    // Written as one block per 16 by 16 cells, by rows then columns, and read back the same.
    Tilemap tilemap;
    grid.write(tilemap);
    REQUIRE(tilemap.blocks.size() == 4);
    CHECK(tilemap.blocks[0].starts_with("-1,-1:"));
    CHECK(tilemap.blocks[1].starts_with("-2,0:"));
    CHECK(tilemap.blocks[1].size() == 5 + 684);
    const TileGrid read = TileGrid::read(tilemap);
    CHECK(read == grid);
    CHECK(read.cells().size() == 5);

    // An emptied block goes away.
    grid.set({16, 0}, 0);
    grid.write(tilemap);
    CHECK(tilemap.blocks.size() == 3);
    grid.clear();
    CHECK(grid.count() == 0);
    CHECK_FALSE(grid.bounds().has_value());
}

TEST_CASE("Single cells change without decoding the whole map, and bad blocks are skipped", "[scene][tilemap]")
{
    Tilemap tilemap;
    devex::scene::setTile(tilemap, {3, 4}, 7);
    devex::scene::setTile(tilemap, {40, -2}, 9);
    CHECK(devex::scene::tileAt(tilemap, {3, 4}) == 7);
    CHECK(devex::scene::tileAt(tilemap, {40, -2}) == 9);
    CHECK(devex::scene::tileAt(tilemap, {5, 5}) == 0);
    CHECK(tilemap.blocks.size() == 2);
    devex::scene::setTile(tilemap, {3, 4}, 0);
    CHECK(tilemap.blocks.size() == 1);

    tilemap.blocks.push_back("not a block");
    tilemap.blocks.push_back("1,1:AAAA");
    const TileGrid grid = TileGrid::read(tilemap);
    CHECK(grid.skippedBlocks() == 2);
    CHECK(grid.count() == 1);
}

TEST_CASE("Cells are found under points of the world and have their middle there", "[scene][tilemap]")
{
    const Tilemap tilemap{.cellSize = {0.5f, 2.0f}};
    const devex::math::Mat4 world = devex::math::translate(devex::math::Mat4{1.0f}, devex::math::Vec3{10.0f, -4.0f, 3.0f});
    CHECK(devex::scene::cellAt(tilemap, world, {10.1f, -3.9f, 3.0f}) == IVec2{0, 0});
    CHECK(devex::scene::cellAt(tilemap, world, {11.2f, 0.5f, 3.0f}) == IVec2{2, 2});
    CHECK(devex::scene::cellAt(tilemap, world, {9.9f, -4.1f, 3.0f}) == IVec2{-1, -1});
    const devex::math::Vec3 center = devex::scene::cellCenter(tilemap, world, {2, 2});
    CHECK_THAT(center.x, WithinAbs(11.25, 1e-5));
    CHECK_THAT(center.y, WithinAbs(1.0, 1e-5));
    CHECK_THAT(center.z, WithinAbs(3.0, 1e-5));
}

TEST_CASE("Tilemaps are saved with their cells and loaded back", "[scene][tilemap]")
{
    devex::scene::Scene scene;
    const devex::scene::Entity entity = scene.createEntity("Level");
    scene.add<devex::scene::Transform>(entity);
    Tilemap tilemap{.cellSize = {0.5f, 0.5f}, .sortingLayer = "Foreground", .order = 2};
    for (int x = -3; x < 20; ++x)
    {
        devex::scene::setTile(tilemap, {x, 0}, static_cast<std::uint16_t>(1 + (x & 3)));
    }
    scene.add<Tilemap>(entity, tilemap);

    const std::string text = devex::scene::saveScene(scene);
    CHECK(text.find("blocks = list(\"-1,0:") != std::string::npos);
    auto loaded = devex::scene::loadScene(text);
    REQUIRE(loaded.has_value());
    const devex::scene::Entity again = loaded->findEntity(scene.uuid(entity));
    REQUIRE(again.isValid());
    const Tilemap& read = loaded->get<Tilemap>(again);
    CHECK(read.blocks == tilemap.blocks);
    CHECK(read.sortingLayer == "Foreground");
    CHECK(devex::scene::tileAt(read, {19, 0}) == 4);
    CHECK(TileGrid::read(read).count() == 23);
}
