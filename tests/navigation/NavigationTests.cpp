#include <devex/asset/Artifact.hpp>
#include <devex/navigation/NavMeshBuilder.hpp>
#include <devex/navigation/NavigationWorld.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/NavigationComponents.hpp>
#include <devex/scene/PhysicsComponents.hpp>
#include <devex/scene/Scene.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>

using devex::math::Vec3;
using devex::navigation::NavigationWorld;
using devex::scene::Entity;
using devex::scene::Scene;

namespace {

constexpr double stepSeconds = 1.0 / 60.0;

Entity addBox(Scene& scene, Vec3 position, Vec3 size)
{
    const Entity box = scene.createEntity("Box");
    scene.add<devex::scene::Transform>(box, devex::scene::Transform{.position = position});
    scene.add<devex::scene::BoxCollider>(box, devex::scene::BoxCollider{.size = size});
    return box;
}

// A floor of 20 by 20 meters whose top is at 0, and a wall across it from its edge at z = -10 to
// z = 4: the way from one side to the other goes around the end of the wall.
struct Level
{
    Scene scene;
    Entity surface;
    devex::asset::AssetId navMeshId = devex::asset::AssetId::generate();
    std::shared_ptr<const devex::asset::NavMeshData> navMesh;
    std::unique_ptr<NavigationWorld> world;

    Level()
    {
        addBox(scene, {0.0f, -0.5f, 0.0f}, {20.0f, 1.0f, 20.0f});
        addBox(scene, {0.0f, 1.0f, -3.0f}, {1.0f, 2.0f, 14.0f});
        surface = scene.createEntity("Navigation");
        scene.add<devex::scene::Transform>(surface);
        scene.add<devex::scene::NavMeshSurface>(surface, devex::scene::NavMeshSurface{.navMesh = navMeshId});
        scene.updateTransforms();
        devex::core::Result<devex::asset::NavMeshData> baked = devex::navigation::bakeNavMesh(
            devex::navigation::collectGeometry(scene, {}), devex::navigation::buildSettingsOf(scene.get<devex::scene::NavMeshSurface>(surface)));
        REQUIRE(baked.has_value());
        navMesh = std::make_shared<const devex::asset::NavMeshData>(std::move(*baked));
        devex::core::Result<std::unique_ptr<NavigationWorld>> created = NavigationWorld::create({
            .navMeshes = [this](devex::asset::AssetId id) { return id == navMeshId ? navMesh : nullptr; },
        });
        REQUIRE(created.has_value());
        world = std::move(*created);
        world->update(scene, devex::core::Duration(0.0));
        REQUIRE(world->hasNavMesh());
    }

    void simulate(double seconds)
    {
        const auto steps = static_cast<int>(seconds / stepSeconds + 0.5);
        for (int step = 0; step < steps; ++step)
        {
            scene.updateTransforms();
            world->update(scene, devex::core::Duration(stepSeconds));
        }
        scene.updateTransforms();
    }

    Entity addAgent(Vec3 position)
    {
        const Entity agent = scene.createEntity("Agent");
        scene.add<devex::scene::Transform>(agent, devex::scene::Transform{.position = position});
        scene.add<devex::scene::NavMeshAgent>(agent);
        scene.updateTransforms();
        return agent;
    }

    [[nodiscard]] Vec3 positionOf(Entity entity) const
    {
        return scene.get<devex::scene::Transform>(entity).position;
    }
};

[[nodiscard]] float flatDistance(Vec3 first, Vec3 second)
{
    return devex::math::length(devex::math::Vec2{first.x - second.x, first.z - second.z});
}

} // namespace

TEST_CASE("Navigation meshes are baked from the colliders that stay put", "[navigation]")
{
    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {20.0f, 1.0f, 20.0f});
    // Neither what moves nor what only detects is scenery.
    const Entity crate = addBox(scene, {3.0f, 0.5f, 3.0f}, {1.0f, 1.0f, 1.0f});
    scene.add<devex::scene::RigidBody>(crate);
    const Entity zone = addBox(scene, {-3.0f, 0.5f, 3.0f}, {2.0f, 2.0f, 2.0f});
    scene.get<devex::scene::BoxCollider>(zone).trigger = true;
    const Entity pillar = scene.createEntity("Pillar");
    scene.add<devex::scene::Transform>(pillar, devex::scene::Transform{.position = {5.0f, 1.0f, 5.0f}});
    scene.add<devex::scene::CylinderCollider>(pillar, devex::scene::CylinderCollider{.radius = 0.5f, .height = 2.0f});
    scene.updateTransforms();

    const devex::navigation::NavGeometry geometry = devex::navigation::collectGeometry(scene, {});
    CHECK(geometry.triangleCount() == 12 + 4 * 12 - 4);
    const devex::core::Result<devex::asset::NavMeshData> baked = devex::navigation::bakeNavMesh(geometry, {});
    REQUIRE(baked.has_value());
    CHECK_FALSE(baked->layers.empty());
    CHECK(baked->tilesX == 3);
    CHECK(baked->tilesZ == 3);
    CHECK(baked->triangles == geometry.triangleCount());

    // The file of a navigation mesh is its cooked asset.
    const devex::core::Result<devex::asset::NavMeshData> decoded = devex::asset::decodeNavMesh(devex::asset::encodeNavMesh(*baked));
    REQUIRE(decoded.has_value());
    CHECK(*decoded == *baked);

    const devex::core::Result<devex::navigation::NavMeshLines> lines = devex::navigation::navMeshLines(*baked);
    REQUIRE(lines.has_value());
    CHECK(lines->polygons > 0);
    CHECK_FALSE(lines->borders.empty());
    // Lines lie on the floor.
    CHECK(std::ranges::all_of(lines->borders, [](Vec3 point) { return std::abs(point.y) < 0.5f; }));

    CHECK_FALSE(devex::navigation::bakeNavMesh({}, {}).has_value());
}

TEST_CASE("Paths along the navigation mesh go around walls", "[navigation]")
{
    Level level;
    const std::vector<Vec3> path = level.world->findPath({-5.0f, 0.0f, -5.0f}, {5.0f, 0.0f, -5.0f});
    REQUIRE(path.size() >= 3);
    CHECK(flatDistance(path.front(), {-5.0f, 0.0f, -5.0f}) < 0.1f);
    CHECK(flatDistance(path.back(), {5.0f, 0.0f, -5.0f}) < 0.1f);
    CHECK(std::ranges::any_of(path, [](Vec3 corner) { return corner.z > 4.0f; }));

    const std::optional<Vec3> onGround = level.world->samplePosition({2.0f, 1.5f, 7.0f}, 3.0f);
    REQUIRE(onGround);
    CHECK(onGround->y == Catch::Approx(0.0f).margin(0.2f));
    CHECK_FALSE(level.world->samplePosition({50.0f, 0.0f, 0.0f}, 1.0f));

    // Straight across, the wall stops the ray, an agent radius before it.
    const std::optional<devex::navigation::NavHit> blocked = level.world->raycast({-5.0f, 0.0f, -5.0f}, {5.0f, 0.0f, -5.0f});
    REQUIRE(blocked);
    CHECK(blocked->position.x == Catch::Approx(-0.9f).margin(0.25f));
    CHECK(blocked->normal.x < -0.9f);
    CHECK_FALSE(level.world->raycast({-5.0f, 0.0f, 6.0f}, {5.0f, 0.0f, 6.0f}));
}

TEST_CASE("Agents walk to their destination and face where they go", "[navigation]")
{
    Level level;
    const Entity agent = level.addAgent({-5.0f, 0.0f, -5.0f});
    // A destination given before the agent first walks.
    REQUIRE(level.world->setDestination(agent, {5.0f, 0.0f, -5.0f}));
    CHECK_FALSE(level.world->setDestination(agent, {60.0f, 0.0f, 0.0f}));
    level.simulate(0.5);
    CHECK(level.world->hasDestination(agent));
    CHECK(level.world->agentCount() == 1);
    CHECK(level.world->remainingDistance(agent) > 15.0f);
    CHECK(devex::math::length(level.scene.get<devex::scene::NavMeshAgent>(agent).velocity) > 0.5f);
    // It walks towards the end of the wall, away from -Z.
    const Vec3 forward = level.scene.get<devex::scene::Transform>(agent).rotation * Vec3{0.0f, 0.0f, -1.0f};
    CHECK(forward.z > 0.3f);

    level.simulate(12.0);
    CHECK(flatDistance(level.positionOf(agent), {5.0f, 0.0f, -5.0f}) < 0.3f);
    CHECK_FALSE(level.world->hasDestination(agent));
    CHECK(level.world->remainingDistance(agent) == 0.0f);

    // Moved by game code, it starts again from there.
    level.scene.get<devex::scene::Transform>(agent).position = {-5.0f, 0.0f, 8.0f};
    REQUIRE(level.world->setDestination(agent, {5.0f, 0.0f, 8.0f}));
    level.simulate(0.2);
    CHECK(flatDistance(level.positionOf(agent), {-5.0f, 0.0f, 8.0f}) < 1.0f);
    level.simulate(6.0);
    CHECK(flatDistance(level.positionOf(agent), {5.0f, 0.0f, 8.0f}) < 0.3f);

    // Stopped, it stays.
    REQUIRE(level.world->setDestination(agent, {-5.0f, 0.0f, 8.0f}));
    level.simulate(0.5);
    level.world->stop(agent);
    level.simulate(1.5);
    const Vec3 stopped = level.positionOf(agent);
    level.simulate(1.0);
    CHECK(flatDistance(level.positionOf(agent), stopped) < 0.05f);
}

TEST_CASE("Agents steer around each other", "[navigation]")
{
    Level level;
    const Entity first = level.addAgent({3.0f, 0.0f, 7.0f});
    const Entity second = level.addAgent({-3.0f, 0.0f, 7.0f});
    REQUIRE(level.world->setDestination(first, {-6.0f, 0.0f, 7.0f}));
    REQUIRE(level.world->setDestination(second, {6.0f, 0.0f, 7.0f}));
    float closest = 100.0f;
    for (int step = 0; step < 360; ++step)
    {
        level.simulate(stepSeconds);
        closest = std::min(closest, flatDistance(level.positionOf(first), level.positionOf(second)));
    }
    // They passed each other without going through: at least about their two radii apart.
    CHECK(closest > 0.6f);
    CHECK(flatDistance(level.positionOf(first), {-6.0f, 0.0f, 7.0f}) < 0.5f);
    CHECK(flatDistance(level.positionOf(second), {6.0f, 0.0f, 7.0f}) < 0.5f);
}

TEST_CASE("Obstacles cut their shape out of the navigation mesh", "[navigation]")
{
    Level level;
    // A block closing the way around the wall.
    const Entity block = level.scene.createEntity("Block");
    level.scene.add<devex::scene::Transform>(block, devex::scene::Transform{.position = {0.0f, 1.0f, 7.0f}});
    level.scene.add<devex::scene::NavMeshObstacle>(block, devex::scene::NavMeshObstacle{.size = {2.0f, 2.0f, 7.0f}});
    level.simulate(0.1);
    CHECK(level.world->obstacleCount() == 1);
    const std::vector<Vec3> closed = level.world->findPath({-5.0f, 0.0f, -5.0f}, {5.0f, 0.0f, -5.0f});
    CHECK((closed.empty() || flatDistance(closed.back(), {5.0f, 0.0f, -5.0f}) > 2.0f));

    // Moved away, it opens the way again.
    level.scene.get<devex::scene::Transform>(block).position = {-7.0f, 1.0f, -7.0f};
    level.simulate(0.1);
    const std::vector<Vec3> opened = level.world->findPath({-5.0f, 0.0f, -5.0f}, {5.0f, 0.0f, -5.0f});
    REQUIRE_FALSE(opened.empty());
    CHECK(flatDistance(opened.back(), {5.0f, 0.0f, -5.0f}) < 0.1f);

    level.scene.remove<devex::scene::NavMeshObstacle>(block);
    level.simulate(0.1);
    CHECK(level.world->obstacleCount() == 0);
}
