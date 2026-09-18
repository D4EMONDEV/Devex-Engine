#include <devex/asset/Primitives.hpp>
#include <devex/physics/PhysicsWorld.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/PhysicsComponents.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <unordered_map>

using devex::math::Vec3;
using devex::physics::ContactPhase;
using devex::physics::PhysicsWorld;
using devex::scene::Entity;
using devex::scene::Scene;

namespace {

constexpr double stepSeconds = 1.0 / 60.0;
// Resting bodies sink into each other by up to Jolt's penetration slop, 2 cm.
constexpr float restingMargin = 0.03f;

[[nodiscard]] std::unique_ptr<PhysicsWorld> makeWorld(devex::asset::PhysicsSettings settings = {})
{
    auto meshes = std::make_shared<std::unordered_map<devex::asset::AssetId, devex::asset::MeshData>>();
    auto world = PhysicsWorld::create({
        .settings = std::move(settings),
        .meshes = [meshes](devex::asset::AssetId id) -> const devex::asset::MeshData* {
            if (const auto found = meshes->find(id); found != meshes->end())
            {
                return &found->second;
            }
            std::optional<devex::asset::MeshData> mesh = devex::asset::makeBuiltinMesh(id);
            return mesh ? &meshes->emplace(id, std::move(*mesh)).first->second : nullptr;
        },
        .threads = 2,
    });
    REQUIRE(world.has_value());
    return std::move(*world);
}

// Runs the steps of a duration, as the runtime does: transforms first, then the simulation.
void simulate(PhysicsWorld& world, Scene& scene, double seconds, std::vector<devex::physics::Contact>* contacts = nullptr)
{
    const auto steps = static_cast<int>(seconds / stepSeconds + 0.5);
    for (int step = 0; step < steps; ++step)
    {
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

Entity addGround(Scene& scene, std::uint32_t layer = 0)
{
    const Entity ground = scene.createEntity("Ground");
    scene.add<devex::scene::Transform>(ground, devex::scene::Transform{.position = {0.0f, -0.5f, 0.0f}});
    scene.add<devex::scene::BoxCollider>(ground, devex::scene::BoxCollider{.size = {20.0f, 1.0f, 20.0f}});
    if (layer != 0)
    {
        scene.add<devex::scene::RigidBody>(ground, devex::scene::RigidBody{.type = devex::scene::BodyType::Static, .layer = layer});
    }
    return ground;
}

Entity addBall(Scene& scene, Vec3 position, std::uint32_t layer = 0)
{
    const Entity ball = scene.createEntity("Ball");
    scene.add<devex::scene::Transform>(ball, devex::scene::Transform{.position = position});
    scene.add<devex::scene::RigidBody>(ball, devex::scene::RigidBody{.layer = layer});
    scene.add<devex::scene::SphereCollider>(ball);
    return ball;
}

[[nodiscard]] bool hasContact(const std::vector<devex::physics::Contact>& contacts, ContactPhase phase, Entity first,
                              Entity second, bool trigger)
{
    return std::ranges::any_of(contacts, [&](const devex::physics::Contact& contact) {
        return contact.phase == phase && contact.involves(first) && contact.other(first) == second &&
               contact.trigger == trigger;
    });
}

} // namespace

TEST_CASE("A dynamic body falls onto static colliders and rests on them", "[physics]")
{
    Scene scene;
    const Entity ground = addGround(scene);
    const Entity ball = addBall(scene, {0.0f, 4.0f, 0.0f});
    const auto world = makeWorld();

    std::vector<devex::physics::Contact> contacts;
    simulate(*world, scene, 0.25, &contacts);
    const float falling = scene.get<devex::scene::Transform>(ball).position.y;
    CHECK(falling < 4.0f);
    CHECK(scene.get<devex::scene::RigidBody>(ball).linearVelocity.y < -1.0f);

    simulate(*world, scene, 3.0, &contacts);
    CHECK(scene.get<devex::scene::Transform>(ball).position.y == Catch::Approx(0.5f).margin(restingMargin));
    CHECK(std::abs(scene.get<devex::scene::RigidBody>(ball).linearVelocity.y) < 0.05f);
    CHECK(hasContact(contacts, ContactPhase::Begin, ball, ground, false));
    CHECK(world->bodyCount() == 2);
}

TEST_CASE("Spinning bodies stay the same bodies, even beyond the speed limits of Jolt", "[physics]")
{
    // A crate turned on two axes, spinning on the ground faster than Jolt allows: the rounding of its
    // world matrices changes at every step, which must not rebuild its body, and its velocity must
    // not stop Jolt, whose limits it exceeds.
    Scene scene;
    const Entity ground = addGround(scene);
    const Entity crate = scene.createEntity("Crate");
    scene.add<devex::scene::Transform>(
        crate, devex::scene::Transform{.position = {0.0f, 0.55f, 0.0f},
                                       .rotation = devex::math::quatFromEulerAngles(devex::math::Vec3{0.3f, 0.7f, 0.0f}),
                                       .scale = {1.3f, 0.7f, 0.9f}});
    scene.add<devex::scene::RigidBody>(crate, devex::scene::RigidBody{.angularVelocity = {5.0f, 90.0f, 7.0f}});
    scene.add<devex::scene::BoxCollider>(crate);
    const auto world = makeWorld();

    std::vector<devex::physics::Contact> contacts;
    simulate(*world, scene, 2.0, &contacts);
    const auto begins = std::ranges::count_if(contacts, [&](const devex::physics::Contact& contact) {
        return contact.phase == ContactPhase::Begin && contact.involves(crate) && contact.other(crate) == ground;
    });
    // It may bounce, but a body rebuilt at every step would touch the ground anew each time.
    CHECK(begins >= 1);
    CHECK(begins <= 3);
    CHECK(world->bodyCount() == 2);
    // Jolt clamped the spin, which the component reads back below its limit.
    CHECK(devex::math::length(scene.get<devex::scene::RigidBody>(crate).angularVelocity) < 47.13f);

    // Game code asking for too much again is held to the limit too.
    scene.get<devex::scene::RigidBody>(crate).angularVelocity = {0.0f, 200.0f, 0.0f};
    simulate(*world, scene, 0.1);
    CHECK(devex::math::length(scene.get<devex::scene::RigidBody>(crate).angularVelocity) < 47.13f);
}

TEST_CASE("Kinematic bodies follow their entity and push dynamic bodies", "[physics]")
{
    Scene scene;
    addGround(scene);
    const Entity crate = scene.createEntity("Crate");
    scene.add<devex::scene::Transform>(crate, devex::scene::Transform{.position = {2.0f, 0.5f, 0.0f}});
    scene.add<devex::scene::RigidBody>(crate);
    scene.add<devex::scene::BoxCollider>(crate);
    const Entity pusher = scene.createEntity("Pusher");
    scene.add<devex::scene::Transform>(pusher, devex::scene::Transform{.position = {0.0f, 0.5f, 0.0f}});
    scene.add<devex::scene::RigidBody>(pusher, devex::scene::RigidBody{.type = devex::scene::BodyType::Kinematic});
    scene.add<devex::scene::BoxCollider>(pusher);
    const auto world = makeWorld();

    for (int step = 0; step < 120; ++step)
    {
        scene.get<devex::scene::Transform>(pusher).position.x += 2.0f * static_cast<float>(stepSeconds);
        simulate(*world, scene, stepSeconds);
    }
    // The pusher went 4 m: it passed the crate's starting place and shoved it along.
    CHECK(scene.get<devex::scene::Transform>(pusher).position.x == Catch::Approx(4.0f).margin(0.01f));
    CHECK(scene.get<devex::scene::Transform>(crate).position.x > 4.5f);
}

TEST_CASE("Triggers report what enters and leaves them without blocking it", "[physics]")
{
    Scene scene;
    const Entity zone = scene.createEntity("Zone");
    scene.add<devex::scene::Transform>(zone, devex::scene::Transform{.position = {0.0f, 2.0f, 0.0f}});
    scene.add<devex::scene::BoxCollider>(zone, devex::scene::BoxCollider{.size = {2.0f, 1.0f, 2.0f}, .trigger = true});
    const Entity ball = addBall(scene, {0.0f, 4.0f, 0.0f});
    const auto world = makeWorld();

    std::vector<devex::physics::Contact> contacts;
    simulate(*world, scene, 1.5, &contacts);
    CHECK(scene.get<devex::scene::Transform>(ball).position.y < 0.0f);
    CHECK(hasContact(contacts, ContactPhase::Begin, zone, ball, true));
    CHECK(hasContact(contacts, ContactPhase::End, zone, ball, true));
}

TEST_CASE("Collision layers decide which bodies touch and what queries find", "[physics]")
{
    devex::asset::PhysicsSettings settings;
    settings.layerNames[1] = "Ghosts";
    settings.setCollides(1, 0, false);
    Scene scene;
    const Entity ground = addGround(scene);
    const Entity ghost = addBall(scene, {0.0f, 2.0f, 0.0f}, 1);
    const Entity solid = addBall(scene, {3.0f, 2.0f, 0.0f});
    const auto world = makeWorld(settings);

    simulate(*world, scene, 2.0);
    CHECK(scene.get<devex::scene::Transform>(ghost).position.y < -2.0f);
    CHECK(scene.get<devex::scene::Transform>(solid).position.y == Catch::Approx(0.5f).margin(restingMargin));

    const auto groundHit = world->raycast({3.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 20.0f);
    REQUIRE(groundHit.has_value());
    CHECK(groundHit->entity == solid);
    CHECK(groundHit->distance == Catch::Approx(9.0f).margin(0.02f));
    CHECK(groundHit->normal.y == Catch::Approx(1.0f).margin(0.01f));
    // Only layer 0, ignoring the ball: the ground under it.
    const auto ignoring = world->raycast({3.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 20.0f, 0b1, solid);
    REQUIRE(ignoring.has_value());
    CHECK(ignoring->entity == ground);
    CHECK_FALSE(world->raycast({3.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 20.0f, 0b10).has_value());

    const auto cast = world->sphereCast({3.0f, 10.0f, 0.0f}, 0.25f, {0.0f, -1.0f, 0.0f}, 20.0f);
    REQUIRE(cast.has_value());
    CHECK(cast->entity == solid);
    const std::vector<Entity> near = world->overlapSphere({3.0f, 0.5f, 0.0f}, 1.0f);
    CHECK(std::ranges::find(near, solid) != near.end());
    CHECK(std::ranges::find(near, ground) != near.end());
}

TEST_CASE("Colliders of children shape the body of their parent", "[physics]")
{
    Scene scene;
    addGround(scene);
    const Entity table = scene.createEntity("Table");
    scene.add<devex::scene::Transform>(table, devex::scene::Transform{.position = {0.0f, 3.0f, 0.0f}});
    scene.add<devex::scene::RigidBody>(table, devex::scene::RigidBody{.mass = 10.0f});
    scene.add<devex::scene::BoxCollider>(table, devex::scene::BoxCollider{.size = {2.0f, 0.2f, 1.0f}});
    for (const float x : {-0.9f, 0.9f})
    {
        const Entity leg = scene.createEntity("Leg");
        REQUIRE(scene.setParent(leg, table));
        scene.add<devex::scene::Transform>(leg, devex::scene::Transform{.position = {x, -0.5f, 0.0f}});
        scene.add<devex::scene::BoxCollider>(leg, devex::scene::BoxCollider{.size = {0.1f, 0.8f, 0.1f}});
    }
    const auto world = makeWorld();

    simulate(*world, scene, 3.0);
    // One body for the table and its legs: it stands on the legs, whose bottom is 0.9 m below the top center.
    CHECK(world->bodyCount() == 2);
    CHECK(scene.get<devex::scene::Transform>(table).position.y == Catch::Approx(0.9f).margin(0.03f));
}

TEST_CASE("Bodies follow the components that game code adds, changes, moves and removes", "[physics]")
{
    Scene scene;
    addGround(scene);
    const Entity ball = addBall(scene, {0.0f, 0.5f, 0.0f});
    const auto world = makeWorld();
    simulate(*world, scene, 0.5);
    CHECK(world->bodyCount() == 2);

    // Moving the entity teleports the body.
    scene.get<devex::scene::Transform>(ball).position = {5.0f, 0.5f, 0.0f};
    simulate(*world, scene, stepSeconds);
    CHECK(scene.get<devex::scene::Transform>(ball).position.x == Catch::Approx(5.0f).margin(0.01f));

    // Setting the velocity launches it; changing the collider rebuilds the body.
    scene.get<devex::scene::RigidBody>(ball).linearVelocity = {0.0f, 5.0f, 0.0f};
    simulate(*world, scene, 0.1);
    CHECK(scene.get<devex::scene::Transform>(ball).position.y > 0.8f);
    scene.get<devex::scene::SphereCollider>(ball).radius = 1.0f;
    simulate(*world, scene, 3.0);
    CHECK(scene.get<devex::scene::Transform>(ball).position.y == Catch::Approx(1.0f).margin(restingMargin));

    // An impulse pushes it sideways.
    world->addImpulse(ball, {10.0f, 0.0f, 0.0f});
    simulate(*world, scene, 0.2);
    CHECK(scene.get<devex::scene::Transform>(ball).position.x > 5.5f);

    scene.remove<devex::scene::SphereCollider>(ball);
    simulate(*world, scene, stepSeconds);
    CHECK(world->bodyCount() == 1);
    scene.destroyEntity(ball);
    simulate(*world, scene, stepSeconds);
    CHECK(world->bodyCount() == 1);
}

TEST_CASE("Characters walk, climb steps and stand on the ground", "[physics]")
{
    Scene scene;
    addGround(scene);
    const Entity step = scene.createEntity("Step");
    scene.add<devex::scene::Transform>(step, devex::scene::Transform{.position = {3.0f, 0.125f, 0.0f}});
    scene.add<devex::scene::BoxCollider>(step, devex::scene::BoxCollider{.size = {2.0f, 0.25f, 4.0f}});
    const Entity player = scene.createEntity("Player");
    scene.add<devex::scene::Transform>(player, devex::scene::Transform{.position = {0.0f, 0.5f, 0.0f}});
    scene.add<devex::scene::CharacterController>(player);
    const auto world = makeWorld();

    // Falls onto the ground.
    simulate(*world, scene, 1.0);
    const devex::scene::CharacterController& controller = scene.get<devex::scene::CharacterController>(player);
    CHECK(controller.grounded);
    CHECK(scene.get<devex::scene::Transform>(player).position.y == Catch::Approx(0.0f).margin(0.05f));
    CHECK(world->bodyCount() == 3);

    // Walks onto the step.
    for (int frame = 0; frame < 90; ++frame)
    {
        scene.get<devex::scene::CharacterController>(player).velocity.x = 2.0f;
        scene.get<devex::scene::CharacterController>(player).velocity.z = 0.0f;
        simulate(*world, scene, stepSeconds);
    }
    CHECK(scene.get<devex::scene::Transform>(player).position.x > 2.5f);
    CHECK(scene.get<devex::scene::Transform>(player).position.y == Catch::Approx(0.25f).margin(0.05f));
    CHECK(scene.get<devex::scene::CharacterController>(player).grounded);

    // Jumps.
    scene.get<devex::scene::CharacterController>(player).velocity = {0.0f, 5.0f, 0.0f};
    simulate(*world, scene, 0.2);
    CHECK(scene.get<devex::scene::Transform>(player).position.y > 0.8f);
    CHECK_FALSE(scene.get<devex::scene::CharacterController>(player).grounded);
}

TEST_CASE("Mesh colliders use the triangles of meshes", "[physics]")
{
    Scene scene;
    const Entity floor = scene.createEntity("Floor");
    scene.add<devex::scene::Transform>(floor, devex::scene::Transform{.scale = {20.0f, 1.0f, 20.0f}});
    scene.add<devex::scene::MeshRenderer>(floor, devex::scene::MeshRenderer{.mesh = devex::asset::builtin::planeMesh});
    scene.add<devex::scene::MeshCollider>(floor);
    const Entity crate = scene.createEntity("Crate");
    scene.add<devex::scene::Transform>(crate, devex::scene::Transform{.position = {1.0f, 3.0f, 1.0f}});
    scene.add<devex::scene::RigidBody>(crate);
    scene.add<devex::scene::MeshCollider>(crate, devex::scene::MeshCollider{.mesh = devex::asset::builtin::cubeMesh, .convex = true});
    const auto world = makeWorld();

    simulate(*world, scene, 3.0);
    CHECK(scene.get<devex::scene::Transform>(crate).position.y == Catch::Approx(0.5f).margin(restingMargin));
}

TEST_CASE("Interpolation places bodies between the last two steps", "[physics]")
{
    Scene scene;
    const Entity ball = addBall(scene, {0.0f, 10.0f, 0.0f});
    const Entity child = scene.createEntity("Marker");
    REQUIRE(scene.setParent(child, ball));
    scene.add<devex::scene::Transform>(child, devex::scene::Transform{.position = {0.0f, 1.0f, 0.0f}});
    const auto world = makeWorld();

    simulate(*world, scene, 0.5);
    const float before = scene.get<devex::scene::Transform>(ball).position.y;
    simulate(*world, scene, stepSeconds);
    const float after = scene.get<devex::scene::Transform>(ball).position.y;
    REQUIRE(after < before);

    world->interpolate(scene, 0.5f);
    const float halfway = scene.get<devex::scene::WorldTransform>(ball).matrix[3].y;
    CHECK(halfway == Catch::Approx((before + after) * 0.5f).margin(0.001f));
    CHECK(scene.get<devex::scene::WorldTransform>(child).matrix[3].y == Catch::Approx(halfway + 1.0f).margin(0.001f));
    // The Transform keeps the simulated pose.
    CHECK(scene.get<devex::scene::Transform>(ball).position.y == after);
}

TEST_CASE("Physics components are saved in scenes", "[physics]")
{
    Scene scene;
    const Entity body = scene.createEntity("Body");
    scene.add<devex::scene::RigidBody>(body, devex::scene::RigidBody{.type = devex::scene::BodyType::Kinematic, .mass = 3.0f, .layer = 2});
    scene.add<devex::scene::CapsuleCollider>(body, devex::scene::CapsuleCollider{.radius = 0.3f, .height = 1.5f, .trigger = true});
    scene.add<devex::scene::CharacterController>(body, devex::scene::CharacterController{.stepHeight = 0.2f});

    const std::string text = devex::scene::saveScene(scene);
    CHECK(text.find("type = \"kinematic\"") != std::string::npos);
    const auto loaded = devex::scene::loadScene(text);
    REQUIRE(loaded.has_value());
    const Entity copy = loaded->findEntity(scene.uuid(body));
    REQUIRE(copy.isValid());
    CHECK(loaded->get<devex::scene::RigidBody>(copy).type == devex::scene::BodyType::Kinematic);
    CHECK(loaded->get<devex::scene::RigidBody>(copy).layer == 2);
    CHECK(loaded->get<devex::scene::CapsuleCollider>(copy).trigger);
    CHECK(loaded->get<devex::scene::CharacterController>(copy).stepHeight == 0.2f);
}
