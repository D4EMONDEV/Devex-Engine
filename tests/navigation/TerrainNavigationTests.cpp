#include <devex/asset/TerrainMaps.hpp>
#include <devex/navigation/NavMeshBuilder.hpp>
#include <devex/navigation/NavigationWorld.hpp>
#include <devex/physics/PhysicsWorld.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/NavigationComponents.hpp>
#include <devex/scene/PhysicsComponents.hpp>
#include <devex/scene/Scene.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>

using devex::math::Vec3;
using devex::scene::Entity;

namespace {

struct TerrainLevel
{
    devex::scene::Scene scene;
    devex::asset::TerrainMaps maps;
    devex::asset::AssetId data = devex::asset::AssetId::generate();
    devex::asset::AssetId navigation = devex::asset::AssetId::generate();
    Entity terrain;
    std::shared_ptr<const devex::asset::NavMeshData> baked;
    std::unique_ptr<devex::navigation::NavigationWorld> world;

    explicit TerrainLevel(std::uint32_t resolution = 33)
        : maps(devex::asset::makeFlatTerrain(resolution))
    {
        terrain = scene.createEntity("Ground");
        scene.add<devex::scene::Transform>(terrain);
        scene.add<devex::scene::Terrain>(terrain, devex::scene::Terrain{.data = data, .size = 32.0f});
        const Entity surface = scene.createEntity("Navigation");
        scene.add<devex::scene::NavMeshSurface>(surface).navMesh = navigation;
    }

    [[nodiscard]] const devex::asset::TerrainMaps* source(devex::asset::AssetId id) const
    {
        return id == data ? &maps : nullptr;
    }

    [[nodiscard]] devex::core::Result<devex::asset::NavMeshData> bakeResult(
        const devex::asset::NavMeshBuildSettings& settings = {})
    {
        scene.updateTransforms();
        return devex::navigation::bakeNavMesh(scene, {}, [this](auto id) { return source(id); }, settings);
    }

    void bake(const devex::asset::NavMeshBuildSettings& settings = {})
    {
        auto result = bakeResult(settings);
        if (!result)
        {
            FAIL(result.error().message);
        }
        baked = std::make_shared<const devex::asset::NavMeshData>(std::move(*result));
        auto created = devex::navigation::NavigationWorld::create({
            .navMeshes = [this](auto id) { return id == navigation ? baked : nullptr; },
        });
        REQUIRE(created.has_value());
        world = std::move(*created);
        world->update(scene, devex::core::Duration(0.0));
        REQUIRE(world->hasNavMesh());
    }
};

[[nodiscard]] float horizontalDistance(Vec3 first, Vec3 second)
{
    return devex::math::length(devex::math::Vec2{first.x - second.x, first.z - second.z});
}

} // namespace

TEST_CASE("Terrain navigation follows the world position and size used by physics", "[navigation][terrain]")
{
    TerrainLevel level;
    std::ranges::fill(level.maps.heights, 2.5f);
    const Entity parent = level.scene.createEntity("Parent");
    level.scene.add<devex::scene::Transform>(parent, devex::scene::Transform{
        .position = {10.0f, 4.0f, -20.0f},
        .rotation = devex::math::angleAxis(devex::math::radians(90.0f), Vec3{0, 1, 0}),
        .scale = {2.0f, 3.0f, 4.0f}});
    REQUIRE(level.scene.setParent(level.terrain, parent));
    auto& transform = level.scene.get<devex::scene::Transform>(level.terrain);
    transform.position = {2.0f, 1.0f, 3.0f};
    transform.rotation = devex::math::angleAxis(0.7f, Vec3{1, 0, 0});
    transform.scale = {4.0f, 2.0f, 0.5f};
    level.bake();
    CHECK(level.baked->triangles == 32 * 32 * 2);
    CHECK(level.baked->origin.x == Catch::Approx(22.0f));
    CHECK(level.baked->origin.z == Catch::Approx(-24.0f));
    CHECK(level.baked->boundsMax.x == Catch::Approx(54.0f));
    CHECK(level.baked->boundsMax.z == Catch::Approx(8.0f));
    const auto ground = level.world->samplePosition({38.0f, 10.0f, -8.0f}, 2.0f);
    REQUIRE(ground.has_value());
    CHECK(ground->y == Catch::Approx(9.5f).margin(0.15f));
}

TEST_CASE("Agents climb terrain ramps across navigation tile boundaries", "[navigation][terrain]")
{
    TerrainLevel level;
    for (std::uint32_t z = 0; z < level.maps.resolution; ++z)
    {
        for (std::uint32_t x = 0; x < level.maps.resolution; ++x)
        {
            level.maps.heights[std::size_t{z} * level.maps.resolution + x] = 0.25f * static_cast<float>(x);
        }
    }
    level.bake();
    const auto path = level.world->findPath({2.0f, 0.5f, 16.0f}, {28.0f, 7.0f, 16.0f});
    REQUIRE(path.size() >= 2);
    CHECK(horizontalDistance(path.back(), {28.0f, 7.0f, 16.0f}) < 0.2f);
    const Entity agent = level.scene.createEntity("Walker");
    level.scene.add<devex::scene::Transform>(agent).position = {2.0f, 0.5f, 16.0f};
    level.scene.add<devex::scene::NavMeshAgent>(agent);
    REQUIRE(level.world->setDestination(agent, {28.0f, 7.0f, 16.0f}));
    for (int step = 0; step < 600; ++step)
    {
        level.scene.updateTransforms();
        level.world->update(level.scene, devex::core::Duration(1.0 / 60.0));
    }
    const Vec3 position = level.scene.get<devex::scene::Transform>(agent).position;
    CHECK(horizontalDistance(position, {28.0f, 7.0f, 16.0f}) < 0.25f);
    CHECK(position.y == Catch::Approx(7.0f).margin(0.25f));
    CHECK_FALSE(level.world->hasDestination(agent));
}

TEST_CASE("Neighbouring terrains share a connected navigation surface", "[navigation][terrain]")
{
    TerrainLevel level;
    const Entity neighbour = level.scene.createEntity("Neighbouring ground");
    level.scene.add<devex::scene::Transform>(neighbour).position = {32.0f, 0.0f, 0.0f};
    level.scene.add<devex::scene::Terrain>(neighbour, devex::scene::Terrain{.data = level.data, .size = 32.0f});
    level.bake();
    CHECK(level.baked->triangles == 2 * 32 * 32 * 2);
    const auto path = level.world->findPath({4.0f, 0.0f, 16.0f}, {60.0f, 0.0f, 16.0f});
    REQUIRE(path.size() >= 2);
    CHECK(horizontalDistance(path.back(), {60.0f, 0.0f, 16.0f}) < 0.2f);
    CHECK_FALSE(level.world->raycast({4.0f, 0.0f, 16.0f}, {60.0f, 0.0f, 16.0f}));
}

TEST_CASE("Terrain holes force paths around the missing ground", "[navigation][terrain]")
{
    TerrainLevel level(65);
    for (std::uint32_t z = 26; z <= 38; ++z)
    {
        for (std::uint32_t x = 26; x <= 38; ++x)
        {
            level.maps.holes[std::size_t{z} * level.maps.resolution + x] = 255;
        }
    }
    level.bake();
    CHECK_FALSE(level.world->samplePosition({16.0f, 0.0f, 16.0f}, 0.2f));
    CHECK(level.world->raycast({4.0f, 0.0f, 16.0f}, {28.0f, 0.0f, 16.0f}).has_value());
    const auto path = level.world->findPath({4.0f, 0.0f, 16.0f}, {28.0f, 0.0f, 16.0f});
    REQUIRE(path.size() >= 3);
    CHECK(horizontalDistance(path.back(), {28.0f, 0.0f, 16.0f}) < 0.2f);
    CHECK(std::ranges::any_of(path, [](Vec3 point) { return std::abs(point.z - 16.0f) > 3.5f; }));
}

TEST_CASE("Terrain slopes above the agent limit separate walkable regions", "[navigation][terrain]")
{
    TerrainLevel level;
    for (std::uint32_t z = 0; z < level.maps.resolution; ++z)
    {
        for (std::uint32_t x = 0; x < level.maps.resolution; ++x)
        {
            level.maps.heights[std::size_t{z} * level.maps.resolution + x] =
                std::clamp(static_cast<float>(x) - 12.0f, 0.0f, 4.0f) * 2.0f;
        }
    }
    level.bake({.agentMaxSlope = devex::math::radians(30.0f)});
    CHECK_FALSE(level.world->samplePosition({14.0f, 4.0f, 16.0f}, 0.2f));
    const auto path = level.world->findPath({4.0f, 0.0f, 16.0f}, {28.0f, 8.0f, 16.0f});
    CHECK((path.empty() || horizontalDistance(path.back(), {28.0f, 8.0f, 16.0f}) > 10.0f));
}

TEST_CASE("Terrain navigation uses the same triangle diagonal and heights as collision", "[navigation][terrain]")
{
    TerrainLevel level;
    level.maps.heights[17 * level.maps.resolution + 17] = 0.5f;
    level.bake({.cellSize = 0.1f, .cellHeight = 0.02f});
    auto physics = devex::physics::PhysicsWorld::create({
        .terrains = [&](auto id) { return level.source(id); }, .threads = 1});
    REQUIRE(physics.has_value());
    (*physics)->step(level.scene, devex::core::Duration(1.0 / 60.0));
    const auto hit = (*physics)->raycast({16.5f, 4.0f, 16.5f}, {0.0f, -1.0f, 0.0f}, 8.0f);
    REQUIRE(hit.has_value());
    const auto point = level.world->samplePosition(hit->point, 1.0f);
    REQUIRE(point.has_value());
    CHECK(point->y == Catch::Approx(hit->point.y).margin(0.08f));
    const auto lines = devex::navigation::navMeshLines(*level.baked);
    REQUIRE(lines.has_value());
    CHECK(std::ranges::any_of(lines->inner, [](Vec3 vertex) { return vertex.y > 0.2f; }));

    const Entity agent = level.scene.createEntity("Walker on the bump");
    level.scene.add<devex::scene::Transform>(agent).position = hit->point;
    level.scene.add<devex::scene::NavMeshAgent>(agent);
    const Entity obstacle = level.scene.createEntity("Obstacle");
    level.scene.add<devex::scene::Transform>(obstacle).position = {18.0f, 0.5f, 18.0f};
    level.scene.add<devex::scene::NavMeshObstacle>(obstacle).size = {0.5f, 1.0f, 0.5f};
    for (int step = 0; step < 15; ++step)
    {
        level.scene.updateTransforms();
        level.world->update(level.scene, devex::core::Duration(1.0 / 60.0));
    }
    CHECK(level.scene.get<devex::scene::Transform>(agent).position.y ==
          Catch::Approx(hit->point.y).margin(0.08f));
    const auto rebuilt = level.world->samplePosition(hit->point, 1.0f);
    REQUIRE(rebuilt.has_value());
    CHECK(rebuilt->y == Catch::Approx(hit->point.y).margin(0.08f));
}

TEST_CASE("A missing terrain sample removes only its incident navigation triangles", "[navigation][terrain]")
{
    TerrainLevel level;
    level.maps.holes[16 * level.maps.resolution + 16] = 255;
    level.bake();
    CHECK(level.baked->triangles == 32 * 32 * 2 - 6);
}

TEST_CASE("Terrain baking reads current sculpted maps and skips disabled or missing data", "[navigation][terrain]")
{
    TerrainLevel level;
    SECTION("Unsaved heights replace the previous ground")
    {
        level.bake();
        std::ranges::fill(level.maps.heights, 3.0f);
        level.bake();
        const auto point = level.world->samplePosition({16.0f, 3.0f, 16.0f}, 0.5f);
        REQUIRE(point.has_value());
        CHECK(point->y == Catch::Approx(3.0f).margin(0.15f));
    }
    SECTION("Disabled collision leaves no walkable terrain")
    {
        level.scene.get<devex::scene::Terrain>(level.terrain).collision = false;
        CHECK_FALSE(level.bakeResult().has_value());
    }
    SECTION("Unavailable terrain data leaves no walkable terrain")
    {
        level.scene.get<devex::scene::Terrain>(level.terrain).data = devex::asset::AssetId::generate();
        CHECK_FALSE(level.bakeResult().has_value());
    }
    SECTION("An entirely holed terrain leaves no ground")
    {
        std::ranges::fill(level.maps.holes, std::uint8_t{255});
        CHECK_FALSE(level.bakeResult().has_value());
    }
    SECTION("Invalid terrain data is reported")
    {
        level.maps.heights.pop_back();
        const auto result = level.bakeResult();
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().message.find("Ground") != std::string::npos);
    }
    SECTION("Invalid sizes are reported before rasterization")
    {
        for (const float size : {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
                                 std::numeric_limits<float>::denorm_min()})
        {
            level.scene.get<devex::scene::Terrain>(level.terrain).size = size;
            const auto result = level.bakeResult();
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().message.find("Ground") != std::string::npos);
        }
    }
}

TEST_CASE("Static walls and terrain share the same navigation bake", "[navigation][terrain]")
{
    TerrainLevel level;
    const Entity wall = level.scene.createEntity("Wall");
    level.scene.add<devex::scene::Transform>(wall).position = {16.0f, 1.0f, 12.0f};
    level.scene.add<devex::scene::BoxCollider>(wall).size = {2.0f, 2.0f, 24.0f};
    level.bake();
    CHECK(level.baked->triangles == 32 * 32 * 2 + 12);
    const auto path = level.world->findPath({4.0f, 0.0f, 16.0f}, {28.0f, 0.0f, 16.0f});
    REQUIRE(path.size() >= 3);
    CHECK(horizontalDistance(path.back(), {28.0f, 0.0f, 16.0f}) < 0.2f);
    CHECK(std::ranges::any_of(path, [](Vec3 point) { return point.z > 24.0f; }));
}

TEST_CASE("Detailed terrain samples bake without a global triangle mesh", "[navigation][terrain]")
{
    TerrainLevel level(513);
    level.bake({.cellSize = 0.5f, .tileSize = 32});
    CHECK(level.baked->triangles == 512 * 512 * 2);
    const auto path = level.world->findPath({2.0f, 0.0f, 16.0f}, {30.0f, 0.0f, 16.0f});
    REQUIRE(path.size() >= 2);
    CHECK(horizontalDistance(path.back(), {30.0f, 0.0f, 16.0f}) < 0.2f);
}
