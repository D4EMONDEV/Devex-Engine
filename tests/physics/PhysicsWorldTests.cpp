#include <devex/asset/Primitives.hpp>
#include <devex/physics/PhysicsWorld.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/JointComponents.hpp>
#include <devex/scene/PhysicsComponents.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
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

    // Rays stop on triggers, except those of particles, which only solid bodies stop.
    const Entity ground = addGround(scene);
    const auto solidWorld = makeWorld();
    simulate(*solidWorld, scene, stepSeconds);
    const auto blocked = solidWorld->raycast({0.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 20.0f);
    REQUIRE(blocked.has_value());
    CHECK(blocked->entity == zone);
    const auto through = solidWorld->raycastSolid({0.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 20.0f);
    REQUIRE(through.has_value());
    CHECK(through->entity == ground);
    CHECK(through->normal.y == Catch::Approx(1.0f).margin(0.01f));
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

namespace {

// A joint entity at a place, turned by an angle around Z, whose component names its bodies.
template <typename Joint>
Entity addJoint(Scene& scene, Vec3 position, Joint joint, float angle = 0.0f)
{
    const Entity entity = scene.createEntity("Joint");
    scene.add<devex::scene::Transform>(
        entity, devex::scene::Transform{.position = position, .rotation = devex::math::angleAxis(angle, Vec3{0.0f, 0.0f, 1.0f})});
    scene.add<Joint>(entity, joint);
    return entity;
}

[[nodiscard]] devex::scene::EntityRef referenceOf(const Scene& scene, Entity entity)
{
    return devex::scene::EntityRef{scene.uuid(entity)};
}

[[nodiscard]] Vec3 positionOf(const Scene& scene, Entity entity)
{
    return scene.get<devex::scene::Transform>(entity).position;
}

} // namespace

TEST_CASE("Hinges swing bodies around their axis, within their limits, turned by their motor", "[physics][joints]")
{
    // A ball two meters right of a hinge on the world swings down around it, in the XY plane.
    Scene scene;
    const Entity ball = addBall(scene, {2.0f, 4.0f, 0.0f});
    const Entity hinge = addJoint(scene, {0.0f, 4.0f, 0.0f}, devex::scene::HingeJoint{.bodyA = referenceOf(scene, ball)});
    const auto world = makeWorld();
    simulate(*world, scene, 0.6);
    CHECK(world->jointCount() == 1);
    const Vec3 swung = positionOf(scene, ball);
    CHECK(swung.y < 3.0f);
    CHECK(devex::math::length(swung - Vec3{0.0f, 4.0f, 0.0f}) == Catch::Approx(2.0f).margin(0.03f));
    CHECK(std::abs(swung.z) < 0.01f);

    // With limits, it stops where they say: a third of a radian down.
    auto& joint = scene.get<devex::scene::HingeJoint>(hinge);
    scene.get<devex::scene::Transform>(ball).position = {2.0f, 4.0f, 0.0f};
    scene.get<devex::scene::RigidBody>(ball).linearVelocity = Vec3{0.0f};
    scene.get<devex::scene::RigidBody>(ball).angularVelocity = Vec3{0.0f};
    joint.useLimits = true;
    joint.lowerAngle = -0.3f;
    joint.upperAngle = 0.3f;
    simulate(*world, scene, 2.0);
    const Vec3 limited = positionOf(scene, ball) - Vec3{0.0f, 4.0f, 0.0f};
    CHECK(std::atan2(limited.y, limited.x) == Catch::Approx(-0.3f).margin(0.05f));

    // Without gravity, the motor turns it at its speed.
    Scene still;
    const Entity wheel = addBall(still, {0.0f, 0.0f, 0.0f});
    addJoint(still, {0.0f, 0.0f, 0.0f},
             devex::scene::HingeJoint{.bodyA = referenceOf(still, wheel), .useMotor = true, .motorSpeed = 2.0f});
    const auto weightless = makeWorld(devex::asset::PhysicsSettings{.gravity = Vec3{0.0f}});
    simulate(*weightless, still, 1.0);
    CHECK(still.get<devex::scene::RigidBody>(wheel).angularVelocity.z == Catch::Approx(2.0f).margin(0.05f));
}

TEST_CASE("Sliders keep bodies on their axis, between their limits, moved by their motor", "[physics][joints]")
{
    Scene scene;
    const Entity box = addBall(scene, {0.0f, 4.0f, 0.0f});
    const Entity slider = addJoint(scene, {0.0f, 4.0f, 0.0f},
                                   devex::scene::SliderJoint{.bodyA = referenceOf(scene, box), .useMotor = true, .motorSpeed = 1.0f});
    const auto world = makeWorld();
    simulate(*world, scene, 1.0);
    // Gravity cannot pull it off its axis; the motor carries it along.
    CHECK(positionOf(scene, box).y == Catch::Approx(4.0f).margin(0.02f));
    CHECK(positionOf(scene, box).x == Catch::Approx(1.0f).margin(0.1f));

    scene.get<devex::scene::SliderJoint>(slider).useLimits = true;
    scene.get<devex::scene::SliderJoint>(slider).upperLimit = 0.5f;
    scene.get<devex::scene::Transform>(box).position = {0.0f, 4.0f, 0.0f};
    simulate(*world, scene, 1.5);
    CHECK(positionOf(scene, box).x == Catch::Approx(0.5f).margin(0.03f));
}

TEST_CASE("Distance joints hold bodies as rods and ropes", "[physics][joints]")
{
    // A rod from the ball to a point of the world two meters above and half a meter aside: the ball
    // swings at its end.
    Scene scene;
    const Entity ball = addBall(scene, {0.5f, 2.0f, 0.0f});
    addJoint(scene, {0.5f, 2.0f, 0.0f}, devex::scene::DistanceJoint{.bodyA = referenceOf(scene, ball), .anchor = {-0.5f, 2.0f, 0.0f}});
    // A rope of two meters a meter above the other ball: it falls a meter, then hangs.
    const Entity dropped = addBall(scene, {5.0f, 3.0f, 0.0f});
    addJoint(scene, {5.0f, 3.0f, 0.0f},
             devex::scene::DistanceJoint{.bodyA = referenceOf(scene, dropped), .anchor = {0.0f, 1.0f, 0.0f}, .length = 2.0f, .rope = true});
    const auto world = makeWorld();
    simulate(*world, scene, 4.0);
    CHECK(devex::math::length(positionOf(scene, ball) - Vec3{0.0f, 4.0f, 0.0f}) == Catch::Approx(std::sqrt(4.25f)).margin(0.03f));
    CHECK(positionOf(scene, ball).y < 2.01f);
    CHECK(positionOf(scene, dropped).y == Catch::Approx(2.0f).margin(0.05f));
    CHECK(world->jointCount() == 2);
}

TEST_CASE("Fixed joints weld bodies until they break, and connected bodies collide only when asked", "[physics][joints]")
{
    Scene scene;
    const Entity box = addBall(scene, {0.0f, 4.0f, 0.0f});
    const Entity weld = addJoint(scene, {0.0f, 4.0f, 0.0f}, devex::scene::FixedJoint{.bodyA = referenceOf(scene, box)});
    const auto world = makeWorld();
    simulate(*world, scene, 1.0);
    CHECK(positionOf(scene, box).y == Catch::Approx(4.0f).margin(0.02f));

    // Too weak for the weight of the ball, it breaks at once, and the ball falls.
    scene.get<devex::scene::FixedJoint>(weld).breakForce = 2.0f;
    scene.updateTransforms();
    world->step(scene, devex::core::Duration(stepSeconds));
    world->step(scene, devex::core::Duration(stepSeconds));
    REQUIRE(world->brokenJoints().size() == 1);
    CHECK(world->brokenJoints().front().joint == weld);
    CHECK(world->brokenJoints().front().bodyA == box);
    CHECK_FALSE(world->brokenJoints().front().bodyB.isValid());
    CHECK(scene.get<devex::scene::FixedJoint>(weld).broken);
    world->clearContacts();
    CHECK(world->brokenJoints().empty());
    simulate(*world, scene, 0.5);
    CHECK(positionOf(scene, box).y < 3.5f);
    CHECK(scene.get<devex::scene::FixedJoint>(weld).broken);
    CHECK(world->jointCount() == 0);

    // Changed, it holds again from where the ball is.
    scene.get<devex::scene::FixedJoint>(weld).breakForce = 0.0f;
    simulate(*world, scene, 0.1);
    const float held = positionOf(scene, box).y;
    simulate(*world, scene, 0.5);
    CHECK_FALSE(scene.get<devex::scene::FixedJoint>(weld).broken);
    CHECK(positionOf(scene, box).y == Catch::Approx(held).margin(0.05f));

    // Two balls that overlap, tied by a hinge: they never touch, unless the joint lets them collide.
    for (const bool collide : {false, true})
    {
        Scene pair;
        const Entity first = addBall(pair, {0.0f, 1.0f, 0.0f});
        const Entity second = addBall(pair, {0.6f, 1.0f, 0.0f});
        addJoint(pair, {0.3f, 1.0f, 0.0f},
                 devex::scene::HingeJoint{.bodyA = referenceOf(pair, first), .bodyB = referenceOf(pair, second), .collideConnected = collide});
        const auto paired = makeWorld(devex::asset::PhysicsSettings{.gravity = Vec3{0.0f}});
        std::vector<devex::physics::Contact> contacts;
        simulate(*paired, pair, 0.2, &contacts);
        CAPTURE(collide);
        CHECK(paired->jointCount() == 1);
        CHECK(hasContact(contacts, ContactPhase::Begin, first, second, false) == collide);
    }
}

TEST_CASE("A body shaped by the colliders of its children swings around a hinge at its origin", "[physics][joints]")
{
    // A pendulum: the body stands at the pivot, its bob three meters below it, let go from 40 degrees.
    Scene scene;
    const Entity pendulum = scene.createEntity("Pendulum");
    scene.add<devex::scene::Transform>(
        pendulum, devex::scene::Transform{.position = {0.0f, 4.5f, 0.0f},
                                          .rotation = devex::math::angleAxis(devex::math::radians(40.0f), Vec3{1.0f, 0.0f, 0.0f})});
    scene.add<devex::scene::RigidBody>(pendulum, devex::scene::RigidBody{.mass = 8.0f});
    const Entity bob = scene.createEntity("Bob");
    REQUIRE(scene.setParent(bob, pendulum).has_value());
    scene.add<devex::scene::Transform>(bob, devex::scene::Transform{.position = {0.0f, -3.0f, 0.0f}});
    scene.add<devex::scene::SphereCollider>(bob);
    // Its Z axis turned along X, the hinge lets it swing in the YZ plane.
    const Entity hinge = addJoint(scene, {0.0f, 4.5f, 0.0f}, devex::scene::HingeJoint{.bodyA = referenceOf(scene, pendulum)});
    scene.get<devex::scene::Transform>(hinge).rotation = devex::math::angleAxis(devex::math::radians(90.0f), Vec3{0.0f, 1.0f, 0.0f});
    const auto world = makeWorld();
    simulate(*world, scene, 0.4);
    const Vec3 bobAt(scene.get<devex::scene::WorldTransform>(bob).matrix[3]);
    CAPTURE(bobAt.x, bobAt.y, bobAt.z);
    // It swung down from z = -1.93 towards the bottom.
    CHECK(bobAt.z > -1.6f);
    CHECK(std::abs(bobAt.x) < 0.01f);
    CHECK(devex::math::length(bobAt - Vec3{0.0f, 4.5f, 0.0f}) == Catch::Approx(3.0f).margin(0.03f));
}
