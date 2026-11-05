#include <devex/physics/PhysicsWorld.hpp>
#include <devex/runtime/AssetManager.hpp>
#include <devex/runtime/GridWorld.hpp>
#include <devex/asset/AssetId.hpp>
#include <devex/navigation/NavMeshBuilder.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/CsgComponents.hpp>
#include <devex/scene/GridMapComponents.hpp>
#include <devex/scene/PhysicsComponents.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <thread>

using Catch::Approx;
using devex::asset::AssetId;
using devex::core::Uuid;
using devex::math::IVec3;
using devex::math::Vec3;
using devex::runtime::AssetManager;
using devex::runtime::GridWorld;
using devex::scene::Entity;
using devex::scene::GeneratedMesh;
using devex::scene::GridMap;
using devex::scene::Scene;
using devex::scene::Transform;

namespace {

const AssetId paletteAsset{Uuid::fromParts(0x11, 0x22)};
const AssetId material{Uuid::fromParts(0x33, 0x44)};

// Three pieces: a block that collides, standing where it should not count; a decoration that does not collide;
// and a pillar whose mesh is on a child, higher up.
[[nodiscard]] std::string paletteText()
{
    Scene source;
    const Entity block = source.createEntity("Block");
    source.add<Transform>(block).position = {50.0f, 50.0f, 50.0f};
    source.add<devex::scene::MeshRenderer>(block).mesh = devex::asset::builtin::cubeMesh;
    source.add<devex::scene::BoxCollider>(block);
    const Entity decoration = source.createEntity("Decoration");
    source.add<Transform>(decoration);
    auto& renderer = source.add<devex::scene::MeshRenderer>(decoration);
    renderer.mesh = devex::asset::builtin::sphereMesh;
    renderer.material = material;
    const Entity pillar = source.createEntity("Pillar");
    source.add<Transform>(pillar);
    const Entity shaft = source.createEntity("Shaft");
    source.add<Transform>(shaft).position = {0.0f, 1.0f, 0.0f};
    source.add<devex::scene::MeshRenderer>(shaft).mesh = devex::asset::builtin::cubeMesh;
    source.add<devex::scene::BoxCollider>(shaft);
    REQUIRE(source.setParent(shaft, pillar));
    return devex::scene::saveScene(source);
}

struct Palette
{
    explicit Palette(std::string source)
        : text(std::move(source))
    {
        devex::scene::clearPrefabCache();
        devex::scene::setPrefabSourceLoader([this](AssetId) -> devex::core::Result<std::string> { return this->text; });
    }
    ~Palette()
    {
        devex::scene::setPrefabSourceLoader({});
        devex::scene::clearPrefabCache();
    }
    Palette(const Palette&) = delete;
    Palette& operator=(const Palette&) = delete;

    std::string text;
};

struct World
{
    Scene scene;
    AssetManager assets{nullptr, nullptr};
    GridWorld grids;
    Entity entity;

    World()
    {
        entity = scene.createEntity("Grid");
        scene.add<Transform>(entity);
        scene.add<devex::scene::WorldTransform>(entity);
        scene.add<GridMap>(entity).palette = paletteAsset;
    }

    [[nodiscard]] GridMap& grid()
    {
        return scene.get<GridMap>(entity);
    }

    [[nodiscard]] const devex::asset::MeshData* collision()
    {
        const auto& result = scene.get<GeneratedMesh>(entity);
        return result.mesh.isValid() ? assets.meshData(result.mesh) : nullptr;
    }
};

} // namespace

TEST_CASE("Cells hold one piece each, and can be emptied", "[grid]")
{
    GridMap grid;
    CHECK(devex::scene::gridCellCount(grid) == 0);
    CHECK_FALSE(devex::scene::findGridCell(grid, {0, 0, 0}).has_value());
    devex::scene::setGridCell(grid, {1, 2, 3}, 4, 1);
    devex::scene::setGridCell(grid, {-1, 0, -5}, 2);
    REQUIRE(devex::scene::gridCellCount(grid) == 2);
    CHECK(devex::scene::findGridCell(grid, {1, 2, 3})->piece == 4);
    CHECK(devex::scene::findGridCell(grid, {-1, 0, -5})->rotation == 0);
    // In a cell that has one, the new one takes its place, and turns go around.
    devex::scene::setGridCell(grid, {1, 2, 3}, 7, 5);
    CHECK(devex::scene::gridCellCount(grid) == 2);
    CHECK(devex::scene::findGridCell(grid, {1, 2, 3})->piece == 7);
    CHECK(devex::scene::findGridCell(grid, {1, 2, 3})->rotation == 1);
    devex::scene::setGridCell(grid, {0, 0, 0}, 0, -1);
    CHECK(devex::scene::findGridCell(grid, {0, 0, 0})->rotation == 3);
    CHECK(devex::scene::removeGridCell(grid, {1, 2, 3}));
    CHECK_FALSE(devex::scene::removeGridCell(grid, {1, 2, 3}));
    CHECK(devex::scene::gridCellCount(grid) == 2);
    CHECK(devex::scene::findGridCell(grid, {-1, 0, -5}).has_value());
}

TEST_CASE("A cell is where it says, its piece at the middle of it, at the bottom", "[grid]")
{
    GridMap grid;
    grid.cellSize = {2.0f, 3.0f, 4.0f};
    CHECK(devex::scene::gridCellAt(grid, {1.0f, 1.0f, 1.0f}) == IVec3(0, 0, 0));
    CHECK(devex::scene::gridCellAt(grid, {2.5f, 3.5f, 9.0f}) == IVec3(1, 1, 2));
    CHECK(devex::scene::gridCellAt(grid, {-0.5f, -0.5f, -0.5f}) == IVec3(-1, -1, -1));
    const Vec3 at = devex::scene::gridCellPosition(grid, {1, 1, -1});
    CHECK(at.x == Approx(3.0f));
    CHECK(at.y == Approx(3.0f));
    CHECK(at.z == Approx(-2.0f));
    // A quarter turn around Y: what looked along -Z looks along -X.
    const auto matrix = devex::scene::gridCellMatrix(grid, {.position = {0, 0, 0}, .piece = 0, .rotation = 1});
    const Vec3 looks = Vec3(matrix * devex::math::Vec4(0.0f, 0.0f, -1.0f, 0.0f));
    CHECK(looks.x == Approx(-1.0f));
    CHECK(looks.z == Approx(0.0f).margin(1.0e-5f));
}

TEST_CASE("The top entities of a scene are the pieces of a palette", "[grid]")
{
    const Palette sources(paletteText());
    const auto palette = devex::scene::gridPalette(paletteAsset);
    REQUIRE(palette != nullptr);
    REQUIRE(palette->pieces.size() == 3);
    CHECK(palette->pieces[0].name == "Block");
    REQUIRE(palette->pieces[0].parts.size() == 1);
    CHECK(palette->pieces[0].parts[0].collides);
    // Where it stands in the palette does not count.
    CHECK(palette->pieces[0].parts[0].transform[3].x == 0.0f);
    CHECK_FALSE(palette->pieces[1].parts[0].collides);
    CHECK(palette->pieces[1].parts[0].material == material);
    // What is under a piece keeps its place in it.
    REQUIRE(palette->pieces[2].parts.size() == 1);
    CHECK(palette->pieces[2].parts[0].transform[3].y == 1.0f);
    // The same while it does not change.
    CHECK(devex::scene::gridPalette(paletteAsset) == palette);
    CHECK(devex::scene::gridPalette(AssetId{}) == nullptr);
}

TEST_CASE("What collides in a grid is one mesh, made again when it changes", "[grid]")
{
    const Palette sources(paletteText());
    World world;
    devex::scene::setGridCell(world.grid(), {0, 0, 0}, 0);
    world.grids.update(world.scene, world.assets);
    REQUIRE(world.collision() != nullptr);
    CHECK(world.collision()->vertices.size() == 24);
    const AssetId first = world.scene.get<GeneratedMesh>(world.entity).mesh;
    // Standing at the middle of its cell, at the bottom, whatever its place in the palette.
    float low = 1.0e9f;
    for (const auto& vertex : world.collision()->vertices)
    {
        low = std::min(low, vertex.position.x);
    }
    CHECK(low == Approx(0.5f));

    // Nothing changes: the same mesh.
    world.grids.update(world.scene, world.assets);
    CHECK(world.scene.get<GeneratedMesh>(world.entity).mesh == first);

    // Another piece that collides, and one that does not.
    devex::scene::setGridCell(world.grid(), {1, 0, 0}, 2);
    devex::scene::setGridCell(world.grid(), {2, 0, 0}, 1);
    world.grids.update(world.scene, world.assets);
    CHECK(world.scene.get<GeneratedMesh>(world.entity).mesh != first);
    CHECK(world.assets.meshData(first) == nullptr);
    CHECK(world.collision()->vertices.size() == 48);

    // A piece the palette does not have is left out, rather than taking everything down.
    devex::scene::setGridCell(world.grid(), {3, 0, 0}, 99);
    world.grids.update(world.scene, world.assets);
    CHECK(world.collision()->vertices.size() == 48);

    // Without collision, nothing.
    world.grid().collision = false;
    world.grids.update(world.scene, world.assets);
    CHECK(world.collision() == nullptr);

    // Gone with its component.
    world.grid().collision = true;
    world.grids.update(world.scene, world.assets);
    const AssetId last = world.scene.get<GeneratedMesh>(world.entity).mesh;
    world.scene.remove<GridMap>(world.entity);
    world.grids.update(world.scene, world.assets);
    CHECK_FALSE(world.scene.has<GeneratedMesh>(world.entity));
    CHECK(world.assets.meshData(last) == nullptr);
}

TEST_CASE("A grid collides where its pieces are, and navigation walks on it", "[grid]")
{
    const Palette sources(paletteText());
    World world;
    devex::scene::setGridCell(world.grid(), {0, 0, 0}, 0);
    world.grids.update(world.scene, world.assets);
    auto physics = devex::physics::PhysicsWorld::create({.settings = {}, .meshes = [&](AssetId id) { return world.assets.meshData(id); }, .threads = 1});
    REQUIRE(physics.has_value());
    (*physics)->step(world.scene, devex::core::Duration(1.0 / 60.0));
    const Vec3 down{0.0f, -1.0f, 0.0f};
    // In the cell, at its middle; not in the next.
    CHECK((*physics)->raycast({1.0f, 5.0f, 1.0f}, down, 10.0f).has_value());
    CHECK_FALSE((*physics)->raycast({3.0f, 5.0f, 1.0f}, down, 10.0f).has_value());
    const auto geometry = devex::navigation::collectGeometry(world.scene, [&](AssetId id) { return world.assets.meshData(id); });
    CHECK(geometry.vertices.size() == 24);
}

TEST_CASE("Cells are kept with the scene, and what they make comes back", "[grid]")
{
    const Palette sources(paletteText());
    World world;
    world.grid().cellSize = {3.0f, 1.0f, 3.0f};
    devex::scene::setGridCell(world.grid(), {4, -1, 2}, 2, 3);
    world.grids.update(world.scene, world.assets);
    const std::string text = devex::scene::saveScene(world.scene);
    CHECK(text.find("GeneratedMesh") == std::string::npos);
    auto loaded = devex::scene::loadScene(text);
    REQUIRE(loaded.has_value());
    const Entity read = loaded->findEntity(world.scene.uuid(world.entity));
    const GridMap& grid = loaded->get<GridMap>(read);
    CHECK(grid.cellSize.x == 3.0f);
    CHECK(grid.palette == paletteAsset);
    REQUIRE(devex::scene::gridCellCount(grid) == 1);
    const auto cell = devex::scene::gridCell(grid, 0);
    CHECK(cell.position == IVec3(4, -1, 2));
    CHECK(cell.piece == 2);
    CHECK(cell.rotation == 3);
}

TEST_CASE("A single piece with a part under it is a piece of its own", "[grid]")
{
    Scene source;
    const Entity block = source.createEntity("Block");
    source.add<Transform>(block);
    source.add<devex::scene::MeshRenderer>(block).mesh = devex::asset::builtin::cubeMesh;
    const Entity top = source.createEntity("Top");
    source.add<Transform>(top).position = {0.0f, 1.0f, 0.0f};
    source.add<devex::scene::MeshRenderer>(top).mesh = devex::asset::builtin::sphereMesh;
    REQUIRE(source.setParent(top, block));
    const Palette palette(devex::scene::saveScene(source));
    const std::shared_ptr<const devex::scene::GridPalette> read = devex::scene::gridPalette(AssetId{Uuid::fromParts(0x33, 0x44)});
    REQUIRE(read);
    REQUIRE(read->pieces.size() == 1);
    CHECK(read->pieces[0].name == "Block");
    CHECK(read->pieces[0].parts.size() == 2);
}
