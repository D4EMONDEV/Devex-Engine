#include <devex/physics2d/Physics2DWorld.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/JointComponents.hpp>
#include <devex/scene/Physics2DComponents.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/TilemapComponents.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <functional>
#include <memory>
#include <unordered_map>

using devex::math::Vec2;
using devex::math::Vec3;
using devex::physics2d::ContactPhase;
using devex::physics2d::Physics2DWorld;
using devex::physics2d::TileRectangle;
using devex::scene::BodyType;
using devex::scene::Entity;
using devex::scene::Scene;

namespace {

constexpr double stepSeconds = 1.0 / 60.0;
// Resting bodies sink into each other by up to Box2D's linear slop, half a centimeter; characters
// keep a skin of about as much.
constexpr float restingMargin = 0.03f;

using Tilesets = std::unordered_map<devex::asset::AssetId, std::shared_ptr<const devex::asset::TilesetData>>;

[[nodiscard]] std::unique_ptr<Physics2DWorld> makeWorld(devex::asset::PhysicsSettings settings = {},
                                                        std::shared_ptr<Tilesets> tilesets = std::make_shared<Tilesets>())
{
    auto world = Physics2DWorld::create({
        .settings = std::move(settings),
        .tilesets = [tilesets](devex::asset::AssetId id) -> std::shared_ptr<const devex::asset::TilesetData> {
            const auto found = tilesets->find(id);
            return found != tilesets->end() ? found->second : nullptr;
        },
    });
    REQUIRE(world.has_value());
    return std::move(*world);
}

// Runs the steps of a duration as the runtime does: game code, transforms, then the simulation.
void simulate(Physics2DWorld& world, Scene& scene, double seconds, std::vector<devex::physics2d::Contact>* contacts = nullptr,
              const std::function<void()>& fixedUpdate = {})
{
    const auto steps = static_cast<int>(seconds / stepSeconds + 0.5);
    for (int step = 0; step < steps; ++step)
    {
        if (fixedUpdate)
        {
            fixedUpdate();
        }
        scene.updateTransforms();
        world.step(scene, devex::core::Duration(stepSeconds));
        if (contacts != nullptr)
        {
            contacts->insert(contacts->end(), world.contacts().begin(), world.contacts().end());
        }
        world.clearContacts();
    }
    scene.updateTransforms();
}

Entity addBox(Scene& scene, std::string name, Vec2 position, Vec2 size, std::optional<BodyType> type = std::nullopt,
              std::uint32_t layer = 0)
{
    const Entity box = scene.createEntity(std::move(name));
    scene.add<devex::scene::Transform>(box, devex::scene::Transform{.position = {position, 0.0f}});
    scene.add<devex::scene::BoxCollider2D>(box, devex::scene::BoxCollider2D{.size = size});
    if (type || layer != 0)
    {
        scene.add<devex::scene::RigidBody2D>(box, devex::scene::RigidBody2D{.type = type.value_or(BodyType::Static), .layer = layer});
    }
    return box;
}

// Ground whose top is at y = 0, from x = -20 to 20.
Entity addGround(Scene& scene)
{
    return addBox(scene, "Ground", {0.0f, -0.5f}, {40.0f, 1.0f});
}

Entity addCharacter(Scene& scene, Vec2 feet)
{
    const Entity character = scene.createEntity("Character");
    scene.add<devex::scene::Transform>(character, devex::scene::Transform{.position = {feet, 0.0f}});
    scene.add<devex::scene::CharacterController2D>(character);
    return character;
}

[[nodiscard]] Vec2 positionOf(const Scene& scene, Entity entity)
{
    return Vec2(scene.get<devex::scene::Transform>(entity).position);
}

[[nodiscard]] bool hasContact(const std::vector<devex::physics2d::Contact>& contacts, ContactPhase phase, Entity first,
                              Entity second, bool trigger)
{
    return std::ranges::any_of(contacts, [&](const devex::physics2d::Contact& contact) {
        return contact.phase == phase && contact.involves(first) && contact.other(first) == second &&
               contact.trigger == trigger;
    });
}

[[nodiscard]] std::vector<TileRectangle> sorted(std::vector<TileRectangle> rectangles)
{
    std::ranges::sort(rectangles, [](const TileRectangle& first, const TileRectangle& second) {
        return std::tie(first.cell.y, first.cell.x) < std::tie(second.cell.y, second.cell.x);
    });
    return rectangles;
}

} // namespace

TEST_CASE("The collider of tiles merges full tiles into rectangles and top tiles into ledges", "[physics2d]")
{
    devex::scene::TileGrid grid;
    // Two full rows of five, then two full tiles, a decoration, and a ledge of two on row 4.
    for (int x = 0; x < 5; ++x)
    {
        grid.set({x, 0}, 1);
        grid.set({x, 1}, 1 | devex::scene::tileFlipX);
    }
    grid.set({0, 2}, 1);
    grid.set({1, 2}, 1);
    grid.set({6, 0}, 3);
    grid.set({2, 4}, 2);
    grid.set({3, 4}, 2);
    // Far away, in other blocks: a column of three full tiles.
    for (int y = 40; y < 43; ++y)
    {
        grid.set({-30, y}, 1);
    }
    const auto collisionOf = [](std::uint32_t tile) {
        return tile == 1   ? devex::asset::TileCollision::Full
               : tile == 2 ? devex::asset::TileCollision::Top
                           : devex::asset::TileCollision::None;
    };

    CHECK(sorted(devex::physics2d::tileRectangles(grid, collisionOf)) ==
          std::vector<TileRectangle>{
              {.cell = {0, 0}, .size = {5, 2}},
              {.cell = {0, 2}, .size = {2, 1}},
              {.cell = {2, 4}, .size = {2, 1}, .oneWay = true},
              {.cell = {-30, 40}, .size = {1, 3}},
          });
}

TEST_CASE("A dynamic 2D body falls onto static colliders and rests on them", "[physics2d]")
{
    Scene scene;
    const Entity ground = addGround(scene);
    const Entity crate = addBox(scene, "Crate", {0.0f, 4.0f}, {1.0f, 1.0f}, BodyType::Dynamic);
    scene.get<devex::scene::Transform>(crate).position.z = 2.0f;
    const auto world = makeWorld();

    std::vector<devex::physics2d::Contact> contacts;
    simulate(*world, scene, 0.25, &contacts);
    CHECK(positionOf(scene, crate).y < 4.0f);
    CHECK(scene.get<devex::scene::RigidBody2D>(crate).linearVelocity.y < -1.0f);

    simulate(*world, scene, 3.0, &contacts);
    CHECK(positionOf(scene, crate).y == Catch::Approx(0.5f).margin(restingMargin));
    CHECK(positionOf(scene, crate).x == Catch::Approx(0.0f).margin(0.01f));
    // The plane of 2D physics is XY: the depth of the entity stays.
    CHECK(scene.get<devex::scene::Transform>(crate).position.z == 2.0f);
    CHECK(std::abs(scene.get<devex::scene::RigidBody2D>(crate).linearVelocity.y) < 0.05f);
    CHECK(hasContact(contacts, ContactPhase::Begin, crate, ground, false));
    CHECK(world->bodyCount() == 2);
}

TEST_CASE("2D bodies turn around Z and roll down slopes", "[physics2d]")
{
    Scene scene;
    addGround(scene);
    // A slope of 45 degrees rising to the left.
    const Entity slope = scene.createEntity("Slope");
    scene.add<devex::scene::Transform>(slope, devex::scene::Transform{.position = {0.0f, 0.0f, 0.0f}});
    scene.add<devex::scene::PolygonCollider2D>(slope, devex::scene::PolygonCollider2D{.points = {{-4.0f, 0.0f}, {0.0f, 0.0f}, {-4.0f, 4.0f}}});
    const Entity ball = scene.createEntity("Ball");
    scene.add<devex::scene::Transform>(ball, devex::scene::Transform{.position = {-3.0f, 3.6f, 0.0f}});
    scene.add<devex::scene::RigidBody2D>(ball, devex::scene::RigidBody2D{.friction = 0.8f});
    scene.add<devex::scene::CircleCollider2D>(ball, devex::scene::CircleCollider2D{.radius = 0.3f});
    const auto world = makeWorld();

    simulate(*world, scene, 1.5);
    CHECK(positionOf(scene, ball).x > 0.0f);
    // It rolled: its angle turned clockwise as it went right.
    CHECK(scene.get<devex::scene::RigidBody2D>(ball).angularVelocity < -1.0f);
    CHECK(scene.get<devex::scene::Transform>(ball).rotation != devex::math::Quat{1.0f, 0.0f, 0.0f, 0.0f});
}

TEST_CASE("2D colliders of children shape the body of their RigidBody2D", "[physics2d]")
{
    Scene scene;
    addGround(scene);
    const Entity cart = scene.createEntity("Cart");
    scene.add<devex::scene::Transform>(cart, devex::scene::Transform{.position = {0.0f, 3.0f, 0.0f}});
    scene.add<devex::scene::RigidBody2D>(cart, devex::scene::RigidBody2D{.fixedRotation = true});
    // Two wheels below the body of the cart, a scaled child.
    for (const float x : {-1.0f, 1.0f})
    {
        const Entity wheel = scene.createEntity("Wheel");
        scene.add<devex::scene::Transform>(wheel, devex::scene::Transform{.position = {x, -0.5f, 0.0f}, .scale = {2.0f, 2.0f, 1.0f}});
        scene.add<devex::scene::CircleCollider2D>(wheel, devex::scene::CircleCollider2D{.radius = 0.25f});
        REQUIRE(scene.setParent(wheel, cart));
    }
    const auto world = makeWorld();

    simulate(*world, scene, 3.0);
    // The wheels, of radius 0.5 once scaled, hold the cart 1 m above the ground.
    CHECK(positionOf(scene, cart).y == Catch::Approx(1.0f).margin(restingMargin));
    CHECK(world->bodyCount() == 2);
}

TEST_CASE("Collision layers of the project let 2D bodies through each other", "[physics2d]")
{
    devex::asset::PhysicsSettings settings;
    settings.setCollides(0, 1, false);
    Scene scene;
    addGround(scene);
    const Entity ghost = addBox(scene, "Ghost", {0.0f, 2.0f}, {1.0f, 1.0f}, BodyType::Dynamic, 1);
    const Entity crate = addBox(scene, "Crate", {3.0f, 2.0f}, {1.0f, 1.0f}, BodyType::Dynamic);
    const auto world = makeWorld(settings);

    simulate(*world, scene, 1.5);
    CHECK(positionOf(scene, ghost).y < -2.0f);
    CHECK(positionOf(scene, crate).y == Catch::Approx(0.5f).margin(restingMargin));
}

TEST_CASE("2D triggers report what enters and leaves them without blocking it", "[physics2d]")
{
    Scene scene;
    const Entity ground = addGround(scene);
    const Entity zone = addBox(scene, "Zone", {0.0f, 3.0f}, {4.0f, 1.0f});
    scene.get<devex::scene::BoxCollider2D>(zone).trigger = true;
    const Entity crate = addBox(scene, "Crate", {0.0f, 6.0f}, {0.5f, 0.5f}, BodyType::Dynamic);
    const auto world = makeWorld();

    std::vector<devex::physics2d::Contact> contacts;
    simulate(*world, scene, 3.0, &contacts);
    CHECK(positionOf(scene, crate).y == Catch::Approx(0.25f).margin(restingMargin));
    CHECK(hasContact(contacts, ContactPhase::Begin, zone, crate, true));
    CHECK(hasContact(contacts, ContactPhase::End, zone, crate, true));
    CHECK(hasContact(contacts, ContactPhase::Begin, crate, ground, false));
    CHECK_FALSE(hasContact(contacts, ContactPhase::Begin, zone, ground, true));
}

TEST_CASE("One-way 2D colliders hold up bodies from above and let them through from below", "[physics2d]")
{
    Scene scene;
    addGround(scene);
    const Entity ledge = addBox(scene, "Ledge", {0.0f, 2.95f}, {4.0f, 0.1f});
    scene.get<devex::scene::BoxCollider2D>(ledge).oneWay = true;
    // Thrown up from below the ledge: it goes through, then lands on it.
    const Entity crate = addBox(scene, "Crate", {0.0f, 1.0f}, {0.5f, 0.5f}, BodyType::Dynamic);
    scene.get<devex::scene::RigidBody2D>(crate).linearVelocity = {0.0f, 9.0f};
    const auto world = makeWorld();

    simulate(*world, scene, 0.3);
    CHECK(positionOf(scene, crate).y > 3.0f);
    simulate(*world, scene, 3.0);
    CHECK(positionOf(scene, crate).y == Catch::Approx(3.25f).margin(restingMargin));
}

TEST_CASE("Game code moves, throws and pushes 2D bodies", "[physics2d]")
{
    Scene scene;
    addGround(scene);
    const Entity crate = addBox(scene, "Crate", {0.0f, 0.5f}, {1.0f, 1.0f}, BodyType::Dynamic);
    const auto world = makeWorld();
    simulate(*world, scene, 0.5);

    // Moved by its Transform: it jumps there.
    scene.get<devex::scene::Transform>(crate).position = {10.0f, 0.5f, 0.0f};
    simulate(*world, scene, 0.1);
    CHECK(positionOf(scene, crate).x == Catch::Approx(10.0f).margin(0.01f));

    // Thrown by its velocity.
    scene.get<devex::scene::RigidBody2D>(crate).linearVelocity = {0.0f, 5.0f};
    simulate(*world, scene, 0.1);
    CHECK(positionOf(scene, crate).y > 0.8f);
    simulate(*world, scene, 2.0);

    // Pushed by an impulse, then by forces.
    world->addImpulse(crate, {-3.0f, 0.0f});
    simulate(*world, scene, 0.2);
    CHECK(positionOf(scene, crate).x < 9.8f);
    const float before = positionOf(scene, crate).x;
    simulate(*world, scene, 1.0, nullptr, [&] { world->addForce(crate, {40.0f, 0.0f}); });
    CHECK(positionOf(scene, crate).x > before + 1.0f);
}

TEST_CASE("Removing 2D colliders removes their bodies and ends their contacts", "[physics2d]")
{
    Scene scene;
    const Entity ground = addGround(scene);
    const Entity crate = addBox(scene, "Crate", {0.0f, 1.0f}, {1.0f, 1.0f}, BodyType::Dynamic);
    const auto world = makeWorld();
    std::vector<devex::physics2d::Contact> contacts;
    simulate(*world, scene, 1.0, &contacts);
    REQUIRE(hasContact(contacts, ContactPhase::Begin, crate, ground, false));

    contacts.clear();
    scene.remove<devex::scene::BoxCollider2D>(crate);
    simulate(*world, scene, 0.1, &contacts);
    CHECK(hasContact(contacts, ContactPhase::End, crate, ground, false));
    CHECK(world->bodyCount() == 1);
    // Without a body, the crate stays where it was.
    const Vec2 kept = positionOf(scene, crate);
    simulate(*world, scene, 0.5);
    CHECK(positionOf(scene, crate) == kept);
}

TEST_CASE("2D rays and circles find bodies by layer", "[physics2d]")
{
    Scene scene;
    const Entity ground = addGround(scene);
    const Entity crate = addBox(scene, "Crate", {3.0f, 0.5f}, {1.0f, 1.0f}, std::nullopt, 2);
    const Entity zone = addBox(scene, "Zone", {-3.0f, 1.0f}, {1.0f, 1.0f});
    scene.get<devex::scene::BoxCollider2D>(zone).trigger = true;
    const auto world = makeWorld();
    simulate(*world, scene, stepSeconds);

    const std::optional<devex::physics2d::RayHit> down = world->raycast({3.0f, 5.0f}, {0.0f, -2.0f}, 10.0f);
    REQUIRE(down);
    CHECK(down->entity == crate);
    CHECK(down->distance == Catch::Approx(4.0f).margin(0.01f));
    CHECK(down->point.y == Catch::Approx(1.0f).margin(0.01f));
    CHECK(down->normal.y == Catch::Approx(1.0f).margin(0.01f));

    const auto ignored = world->raycast({3.0f, 5.0f}, {0.0f, -1.0f}, 10.0f, devex::physics2d::allLayers, crate);
    REQUIRE(ignored);
    CHECK(ignored->entity == ground);
    const auto masked = world->raycast({3.0f, 5.0f}, {0.0f, -1.0f}, 10.0f, 1u << 0);
    REQUIRE(masked);
    CHECK(masked->entity == ground);
    CHECK_FALSE(world->raycast({3.0f, 5.0f}, {0.0f, -1.0f}, 3.0f));
    // Rays go through triggers.
    const auto throughZone = world->raycast({-3.0f, 5.0f}, {0.0f, -1.0f}, 10.0f);
    REQUIRE(throughZone);
    CHECK(throughZone->entity == ground);

    const std::vector<Entity> near = world->overlapCircle({0.0f, 1.0f}, 3.5f);
    CHECK(std::ranges::count(near, ground) == 1);
    CHECK(std::ranges::count(near, crate) == 1);
    CHECK(std::ranges::count(near, zone) == 1);
    CHECK(world->overlapCircle({0.0f, 1.0f}, 3.5f, 1u << 2) == std::vector<Entity>{crate});
}

TEST_CASE("Interpolation places 2D bodies between their last two steps", "[physics2d]")
{
    Scene scene;
    const Entity crate = addBox(scene, "Crate", {0.0f, 10.0f}, {1.0f, 1.0f}, BodyType::Dynamic);
    scene.get<devex::scene::RigidBody2D>(crate).linearVelocity = {6.0f, 0.0f};
    scene.get<devex::scene::RigidBody2D>(crate).gravityScale = 0.0f;
    const auto world = makeWorld();
    simulate(*world, scene, stepSeconds * 2);
    const float x = positionOf(scene, crate).x;

    world->interpolate(scene, 0.5f);
    const float between = scene.get<devex::scene::WorldTransform>(crate).matrix[3].x;
    CHECK(between == Catch::Approx(x - 0.05f).margin(1e-3f));
    // Interpolation leaves the Transform alone.
    CHECK(positionOf(scene, crate).x == x);
}

TEST_CASE("A 2D character falls, stands, walks and jumps", "[physics2d]")
{
    Scene scene;
    addGround(scene);
    const Entity hero = addCharacter(scene, {0.0f, 2.0f});
    const auto world = makeWorld();

    simulate(*world, scene, 1.0);
    auto& controller = scene.get<devex::scene::CharacterController2D>(hero);
    CHECK(controller.grounded);
    CHECK(controller.groundNormal.y == Catch::Approx(1.0f).margin(0.01f));
    CHECK(positionOf(scene, hero).y == Catch::Approx(0.0f).margin(restingMargin));
    CHECK(controller.velocity.y == 0.0f);

    simulate(*world, scene, 1.0, nullptr, [&] { controller.velocity.x = 3.0f; });
    CHECK(positionOf(scene, hero).x == Catch::Approx(3.0f).margin(0.1f));
    CHECK(positionOf(scene, hero).y == Catch::Approx(0.0f).margin(restingMargin));
    CHECK(controller.grounded);

    controller.velocity = {0.0f, 6.0f};
    simulate(*world, scene, 0.2);
    CHECK_FALSE(controller.grounded);
    CHECK(positionOf(scene, hero).y > 0.8f);
    simulate(*world, scene, 1.5);
    CHECK(controller.grounded);
    CHECK(positionOf(scene, hero).y == Catch::Approx(0.0f).margin(restingMargin));
}

TEST_CASE("A 2D character slides along walls and hits ceilings", "[physics2d]")
{
    Scene scene;
    addGround(scene);
    addBox(scene, "Wall", {5.0f, 1.5f}, {1.0f, 3.0f});
    addBox(scene, "Ceiling", {-5.0f, 2.0f}, {4.0f, 0.5f});
    const Entity hero = addCharacter(scene, {0.0f, 0.0f});
    const auto world = makeWorld();
    auto& controller = scene.get<devex::scene::CharacterController2D>(hero);

    simulate(*world, scene, 3.0, nullptr, [&] { controller.velocity.x = 4.0f; });
    // Against the wall, whose face is at x = 4.5, by its radius.
    CHECK(positionOf(scene, hero).x == Catch::Approx(4.5f - controller.radius).margin(0.05f));
    CHECK(controller.velocity.x == Catch::Approx(0.0f).margin(0.01f));
    CHECK(controller.grounded);

    // Jumping under the ceiling, whose bottom is at y = 1.75: the head stops there.
    scene.get<devex::scene::Transform>(hero).position = {-5.0f, 0.0f, 0.0f};
    simulate(*world, scene, 0.2, nullptr, [&] { controller.velocity.x = 0.0f; });
    controller.velocity = {0.0f, 8.0f};
    float highest = 0.0f;
    simulate(*world, scene, 0.4, nullptr, [&] { highest = std::max(highest, positionOf(scene, hero).y); });
    CHECK(highest + controller.height == Catch::Approx(1.75f).margin(0.05f));
}

TEST_CASE("A 2D character climbs steps and slopes, and not walls", "[physics2d]")
{
    Scene scene;
    addGround(scene);
    const Entity step = addBox(scene, "Step", {5.0f, 0.1f}, {6.0f, 0.2f});
    const Entity hero = addCharacter(scene, {0.0f, 0.0f});
    const auto world = makeWorld();
    auto& controller = scene.get<devex::scene::CharacterController2D>(hero);

    simulate(*world, scene, 1.5, nullptr, [&] { controller.velocity.x = 3.0f; });
    CHECK(positionOf(scene, hero).x > 3.0f);
    CHECK(positionOf(scene, hero).y == Catch::Approx(0.2f).margin(restingMargin));
    CHECK(controller.grounded);

    // Too high a step is a wall.
    scene.get<devex::scene::Transform>(step).scale = {1.0f, 3.0f, 1.0f};
    scene.get<devex::scene::Transform>(step).position = {5.0f, 0.3f, 0.0f};
    scene.get<devex::scene::Transform>(hero).position = {0.0f, 0.0f, 0.0f};
    simulate(*world, scene, 2.0, nullptr, [&] { controller.velocity.x = 3.0f; });
    CHECK(positionOf(scene, hero).x == Catch::Approx(2.0f - controller.radius).margin(0.05f));

    // Up a slope of 30 degrees, and down again on the other side, on the ground all the way.
    scene.destroyEntity(step);
    const Entity hill = scene.createEntity("Hill");
    scene.add<devex::scene::Transform>(hill);
    scene.add<devex::scene::PolygonCollider2D>(
        hill, devex::scene::PolygonCollider2D{.points = {{4.0f, 0.0f}, {12.0f, 0.0f}, {8.0f, 4.0f * 0.57735f}}});
    scene.get<devex::scene::Transform>(hero).position = {0.0f, 0.0f, 0.0f};
    simulate(*world, scene, 0.2, nullptr, [&] { controller.velocity.x = 0.0f; });
    bool alwaysGrounded = true;
    float highest = 0.0f;
    simulate(*world, scene, 5.0, nullptr, [&] {
        controller.velocity.x = 3.0f;
        alwaysGrounded = alwaysGrounded && controller.grounded;
        highest = std::max(highest, positionOf(scene, hero).y);
    });
    CHECK(positionOf(scene, hero).x > 12.0f);
    CHECK(highest > 2.0f);
    CHECK(alwaysGrounded);
}

TEST_CASE("A 2D character jumps through one-way ledges and lands on them", "[physics2d]")
{
    Scene scene;
    addGround(scene);
    const Entity ledge = addBox(scene, "Ledge", {0.0f, 1.95f}, {4.0f, 0.1f});
    scene.get<devex::scene::BoxCollider2D>(ledge).oneWay = true;
    const Entity hero = addCharacter(scene, {0.0f, 0.0f});
    const auto world = makeWorld();
    auto& controller = scene.get<devex::scene::CharacterController2D>(hero);
    simulate(*world, scene, 0.2);
    REQUIRE(controller.grounded);

    controller.velocity = {0.0f, 8.0f};
    simulate(*world, scene, 2.0);
    CHECK(controller.grounded);
    CHECK(positionOf(scene, hero).y == Catch::Approx(2.0f).margin(restingMargin));

    // It walks along the ledge, and off it.
    simulate(*world, scene, 2.0, nullptr, [&] { controller.velocity.x = 3.0f; });
    CHECK(positionOf(scene, hero).x > 4.0f);
    CHECK(positionOf(scene, hero).y == Catch::Approx(0.0f).margin(restingMargin));
}

TEST_CASE("A 2D character rides moving platforms and pushes crates", "[physics2d]")
{
    Scene scene;
    const Entity platform = addBox(scene, "Platform", {0.0f, -0.25f}, {4.0f, 0.5f}, BodyType::Kinematic);
    const Entity hero = addCharacter(scene, {0.0f, 0.5f});
    const auto world = makeWorld();
    auto& controller = scene.get<devex::scene::CharacterController2D>(hero);
    simulate(*world, scene, 0.5);
    REQUIRE(controller.grounded);

    const float start = positionOf(scene, hero).x;
    simulate(*world, scene, 1.0, nullptr,
             [&] { scene.get<devex::scene::Transform>(platform).position.x += 2.0f * static_cast<float>(stepSeconds); });
    CHECK(positionOf(scene, platform).x == Catch::Approx(2.0f).margin(0.02f));
    CHECK(positionOf(scene, hero).x == Catch::Approx(start + 2.0f).margin(0.1f));
    CHECK(controller.grounded);

    Scene crates;
    addGround(crates);
    const Entity crate = addBox(crates, "Crate", {2.0f, 0.5f}, {1.0f, 1.0f}, BodyType::Dynamic);
    const Entity pusher = addCharacter(crates, {0.0f, 0.0f});
    auto& pushing = crates.get<devex::scene::CharacterController2D>(pusher);
    simulate(*world, crates, 2.0, nullptr, [&] { pushing.velocity.x = 2.0f; });
    CHECK(positionOf(crates, crate).x > 3.5f);
    CHECK(positionOf(crates, pusher).x > 2.5f);
}

TEST_CASE("2D characters enter triggers", "[physics2d]")
{
    Scene scene;
    addGround(scene);
    const Entity coin = scene.createEntity("Coin");
    scene.add<devex::scene::Transform>(coin, devex::scene::Transform{.position = {3.0f, 0.5f, 0.0f}});
    scene.add<devex::scene::CircleCollider2D>(coin, devex::scene::CircleCollider2D{.radius = 0.3f, .trigger = true});
    const Entity hero = addCharacter(scene, {0.0f, 0.0f});
    const auto world = makeWorld();
    auto& controller = scene.get<devex::scene::CharacterController2D>(hero);

    std::vector<devex::physics2d::Contact> contacts;
    simulate(*world, scene, 3.0, &contacts, [&] { controller.velocity.x = 3.0f; });
    CHECK(hasContact(contacts, ContactPhase::Begin, coin, hero, true));
    CHECK(hasContact(contacts, ContactPhase::End, coin, hero, true));
    CHECK(positionOf(scene, hero).x > 7.0f);
}

TEST_CASE("Tilemap colliders follow the tiles of their tileset", "[physics2d]")
{
    auto tilesets = std::make_shared<Tilesets>();
    const devex::asset::AssetId tilesetId = devex::asset::AssetId::generate();
    auto tileset = std::make_shared<devex::asset::TilesetData>();
    tileset->tiles = {{.id = 1, .collision = devex::asset::TileCollision::Full},
                      {.id = 2, .collision = devex::asset::TileCollision::Top},
                      {.id = 3}};
    (*tilesets)[tilesetId] = tileset;

    Scene scene;
    const Entity level = scene.createEntity("Level");
    scene.add<devex::scene::Transform>(level, devex::scene::Transform{.position = {-10.0f, -1.0f, 0.0f}});
    devex::scene::Tilemap tilemap{.tileset = tilesetId, .cellSize = {0.5f, 0.5f}};
    devex::scene::TileGrid grid;
    // Ground two cells thick from x = -10 to 10, with grass that is walked through, and a ledge of
    // four cells at y = 1.
    for (int x = 0; x < 40; ++x)
    {
        grid.set({x, 0}, 1);
        grid.set({x, 1}, 1);
        grid.set({x, 2}, 3);
    }
    for (int x = 20; x < 24; ++x)
    {
        grid.set({x, 5}, 2);
    }
    grid.write(tilemap);
    scene.add<devex::scene::Tilemap>(level, tilemap);
    scene.add<devex::scene::TilemapCollider2D>(level);
    const Entity crate = addBox(scene, "Crate", {-5.0f, 3.0f}, {0.5f, 0.5f}, BodyType::Dynamic);
    const Entity onLedge = addBox(scene, "Crate on the ledge", {0.5f, 3.0f}, {0.5f, 0.5f}, BodyType::Dynamic);
    const Entity hero = addCharacter(scene, {5.0f, 2.0f});
    const auto world = makeWorld({}, tilesets);

    std::vector<devex::physics2d::Contact> contacts;
    simulate(*world, scene, 2.0, &contacts);
    // The ground's top is at y = 0; the ledge's at y = 2.
    CHECK(positionOf(scene, crate).y == Catch::Approx(0.25f).margin(restingMargin));
    CHECK(positionOf(scene, onLedge).y == Catch::Approx(2.25f).margin(restingMargin));
    CHECK(positionOf(scene, hero).y == Catch::Approx(0.0f).margin(restingMargin));
    CHECK(scene.get<devex::scene::CharacterController2D>(hero).grounded);
    CHECK(hasContact(contacts, ContactPhase::Begin, crate, level, false));

    // Digging a hole under the crate: it falls into it.
    for (int x = 9; x < 12; ++x)
    {
        grid.set({x, 1}, 0);
        grid.set({x, 0}, 0);
    }
    grid.write(scene.get<devex::scene::Tilemap>(level));
    simulate(*world, scene, 1.0);
    CHECK(positionOf(scene, crate).y < -1.0f);
}

TEST_CASE("A 2D character does not walk up slopes steeper than its maximum", "[physics2d]")
{
    Scene scene;
    addGround(scene);
    // A ramp of 60 degrees rising to the right from x = 4, beyond the 50 degrees of the character.
    const Entity ramp = scene.createEntity("Ramp");
    scene.add<devex::scene::Transform>(ramp);
    scene.add<devex::scene::PolygonCollider2D>(ramp, devex::scene::PolygonCollider2D{.points = {{4.0f, 0.0f}, {8.0f, 0.0f}, {8.0f, 6.9282f}}});
    const Entity hero = addCharacter(scene, {0.0f, 0.0f});
    const auto world = makeWorld();
    auto& controller = scene.get<devex::scene::CharacterController2D>(hero);

    float highest = 0.0f;
    simulate(*world, scene, 3.0, nullptr, [&] {
        controller.velocity.x = 3.0f;
        highest = std::max(highest, positionOf(scene, hero).y);
    });
    CHECK(highest < 0.3f);
    CHECK(positionOf(scene, hero).x < 4.3f);

    // Dropped onto it, it slides down.
    scene.get<devex::scene::Transform>(hero).position = {6.0f, 5.0f, 0.0f};
    controller.velocity = {0.0f, 0.0f};
    simulate(*world, scene, 3.0);
    CHECK(positionOf(scene, hero).y == Catch::Approx(0.0f).margin(restingMargin));
    CHECK(positionOf(scene, hero).x < 4.0f);
}

TEST_CASE("A 2D character jumping just short of a one-way ledge falls back, and walks off its corner", "[physics2d]")
{
    Scene scene;
    addBox(scene, "Ground", {0.0f, -3.5f}, {40.0f, 1.0f});
    const Entity ledge = addBox(scene, "Ledge", {2.0f, -0.05f}, {4.0f, 0.1f});
    scene.get<devex::scene::BoxCollider2D>(ledge).oneWay = true;
    const Entity hero = addCharacter(scene, {-0.1f, -3.0f});
    auto& controller = scene.get<devex::scene::CharacterController2D>(hero);
    controller.gravityScale = 3.0f;
    const auto world = makeWorld();
    simulate(*world, scene, 0.3);

    // Its feet stop two centimeters under the top of the ledge: back to the ground, free to walk.
    controller.velocity = {0.0f, 13.5f};
    simulate(*world, scene, 1.5);
    CHECK(controller.grounded);
    CHECK(positionOf(scene, hero).y == Catch::Approx(-3.0f).margin(restingMargin));
    simulate(*world, scene, 0.2, nullptr, [&] { controller.velocity.x = 5.0f; });
    CHECK(positionOf(scene, hero).x > 0.7f);

    // A little higher, it lands on the corner of the ledge, and walks on along it.
    scene.get<devex::scene::Transform>(hero).position = {-0.1f, -3.0f, 0.0f};
    controller.velocity = {0.0f, 0.0f};
    simulate(*world, scene, 0.3);
    controller.velocity = {0.0f, 13.62f};
    simulate(*world, scene, 1.0);
    CHECK(controller.grounded);
    CHECK(positionOf(scene, hero).y == Catch::Approx(0.0f).margin(restingMargin));
    simulate(*world, scene, 0.4, nullptr, [&] { controller.velocity.x = 5.0f; });
    CHECK(positionOf(scene, hero).x > 1.5f);
    CHECK(positionOf(scene, hero).y == Catch::Approx(0.0f).margin(restingMargin));
}

namespace {

template <typename Joint>
Entity addJoint2D(Scene& scene, Vec2 position, Joint joint)
{
    const Entity entity = scene.createEntity("Joint");
    scene.add<devex::scene::Transform>(entity, devex::scene::Transform{.position = {position, 0.0f}});
    scene.add<Joint>(entity, joint);
    return entity;
}

Entity addDisc(Scene& scene, Vec2 position)
{
    const Entity disc = scene.createEntity("Disc");
    scene.add<devex::scene::Transform>(disc, devex::scene::Transform{.position = {position, 0.0f}});
    scene.add<devex::scene::RigidBody2D>(disc);
    scene.add<devex::scene::CircleCollider2D>(disc);
    return disc;
}

[[nodiscard]] devex::scene::EntityRef referenceOf(const Scene& scene, Entity entity)
{
    return devex::scene::EntityRef{scene.uuid(entity)};
}

} // namespace

TEST_CASE("2D hinges swing bodies within their limits, turned by their motor", "[physics2d][joints]")
{
    Scene scene;
    const Entity disc = addDisc(scene, {2.0f, 4.0f});
    const Entity hinge = addJoint2D(scene, {0.0f, 4.0f}, devex::scene::HingeJoint2D{.bodyA = referenceOf(scene, disc)});
    const auto world = makeWorld();
    simulate(*world, scene, 0.6);
    CHECK(world->jointCount() == 1);
    CHECK(positionOf(scene, disc).y < 3.0f);
    CHECK(devex::math::length(positionOf(scene, disc) - Vec2{0.0f, 4.0f}) == Catch::Approx(2.0f).margin(0.03f));

    scene.get<devex::scene::Transform>(disc).position = {2.0f, 4.0f, 0.0f};
    scene.get<devex::scene::RigidBody2D>(disc).linearVelocity = Vec2{0.0f};
    scene.get<devex::scene::RigidBody2D>(disc).angularVelocity = 0.0f;
    auto& joint = scene.get<devex::scene::HingeJoint2D>(hinge);
    joint.useLimits = true;
    joint.lowerAngle = -0.3f;
    joint.upperAngle = 0.3f;
    simulate(*world, scene, 2.0);
    const Vec2 limited = positionOf(scene, disc) - Vec2{0.0f, 4.0f};
    CHECK(std::atan2(limited.y, limited.x) == Catch::Approx(-0.3f).margin(0.05f));

    Scene still;
    const Entity wheel = addDisc(still, {0.0f, 0.0f});
    addJoint2D(still, {0.0f, 0.0f}, devex::scene::HingeJoint2D{.bodyA = referenceOf(still, wheel), .useMotor = true, .motorSpeed = 2.0f});
    const auto weightless = makeWorld(devex::asset::PhysicsSettings{.gravity = devex::math::Vec3{0.0f}});
    simulate(*weightless, still, 1.0);
    CHECK(still.get<devex::scene::RigidBody2D>(wheel).angularVelocity == Catch::Approx(2.0f).margin(0.05f));
}

TEST_CASE("2D sliders, distance joints and welds hold bodies, and break past their force", "[physics2d][joints]")
{
    Scene scene;
    const Entity carried = addDisc(scene, {0.0f, 4.0f});
    addJoint2D(scene, {0.0f, 4.0f},
               devex::scene::SliderJoint2D{.bodyA = referenceOf(scene, carried), .useMotor = true, .motorSpeed = 1.0f});
    const Entity dropped = addDisc(scene, {5.0f, 3.0f});
    addJoint2D(scene, {5.0f, 3.0f},
               devex::scene::DistanceJoint2D{.bodyA = referenceOf(scene, dropped), .anchor = {0.0f, 1.0f}, .length = 2.0f, .rope = true});
    const Entity welded = addDisc(scene, {-5.0f, 4.0f});
    const Entity weld = addJoint2D(scene, {-5.0f, 4.0f}, devex::scene::FixedJoint2D{.bodyA = referenceOf(scene, welded)});
    const auto world = makeWorld();
    simulate(*world, scene, 2.0);
    CHECK(positionOf(scene, carried).y == Catch::Approx(4.0f).margin(0.02f));
    CHECK(positionOf(scene, carried).x == Catch::Approx(2.0f).margin(0.15f));
    CHECK(positionOf(scene, dropped).y == Catch::Approx(2.0f).margin(0.05f));
    CHECK(positionOf(scene, welded).y == Catch::Approx(4.0f).margin(0.02f));
    CHECK(world->jointCount() == 3);

    scene.get<devex::scene::FixedJoint2D>(weld).breakForce = 2.0f;
    std::vector<Entity> broken;
    for (int step = 0; step < 3; ++step)
    {
        scene.updateTransforms();
        world->step(scene, devex::core::Duration(stepSeconds));
        for (const devex::physics2d::JointBreak& joint : world->brokenJoints())
        {
            broken.push_back(joint.joint);
            CHECK(joint.bodyA == welded);
        }
        world->clearContacts();
    }
    CHECK(broken == std::vector<Entity>{weld});
    CHECK(scene.get<devex::scene::FixedJoint2D>(weld).broken);
    simulate(*world, scene, 0.5);
    CHECK(positionOf(scene, welded).y < 3.5f);
    CHECK(world->jointCount() == 2);

    // Two discs that overlap, tied by a hinge: they collide only when the joint lets them.
    for (const bool collide : {false, true})
    {
        Scene pair;
        const Entity first = addDisc(pair, {0.0f, 1.0f});
        const Entity second = addDisc(pair, {0.6f, 1.0f});
        addJoint2D(pair, {0.3f, 1.0f},
                   devex::scene::HingeJoint2D{.bodyA = referenceOf(pair, first), .bodyB = referenceOf(pair, second), .collideConnected = collide});
        const auto paired = makeWorld(devex::asset::PhysicsSettings{.gravity = devex::math::Vec3{0.0f}});
        std::vector<devex::physics2d::Contact> contacts;
        simulate(*paired, pair, 0.2, &contacts);
        CAPTURE(collide);
        CHECK(std::ranges::any_of(contacts, [&](const devex::physics2d::Contact& contact) {
                  return contact.phase == devex::physics2d::ContactPhase::Begin && contact.involves(first) && contact.other(first) == second;
              }) == collide);
    }
}
