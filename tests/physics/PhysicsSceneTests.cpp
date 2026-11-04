#include "physics/PhysicsScene.hpp"

#include <devex/scene/Physics2DComponents.hpp>
#include <devex/scene/PhysicsComponents.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace devex;

TEST_CASE("Physics writes parent-relative poses and propagates them through grouping entities",
          "[physics][physics2d][hierarchy]")
{
    scene::Scene scene;
    const auto parent = scene.createEntity("Parent");
    scene.add<scene::Transform>(parent,
                                scene::Transform{.position = {3.0f, 4.0f, 5.0f}, .scale = {2.0f, 2.0f, 2.0f}});
    scene.add<scene::RigidBody>(parent);
    const auto group = scene.createEntity("Group");
    REQUIRE(scene.setParent(group, parent));
    const auto body = scene.createEntity("Body");
    REQUIRE(scene.setParent(body, group));
    const auto rotation = math::angleAxis(0.25f, math::Vec3{0.0f, 0.0f, 1.0f});
    scene.add<scene::Transform>(body, scene::Transform{.rotation = rotation, .scale = {1.0f, 2.0f, 3.0f}});
    scene.add<scene::RigidBody2D>(body);
    const auto child = scene.createEntity("Marker");
    REQUIRE(scene.setParent(child, body));
    scene.add<scene::Transform>(child, scene::Transform{.position = {0.0f, 1.0f, 0.0f}});
    scene.updateTransforms();
    CHECK(physics::detail::bodyOwner<scene::RigidBody>(scene, child) == parent);
    CHECK(physics::detail::bodyOwner<scene::RigidBody2D>(scene, child) == body);
    const math::Mat4 target = math::composeTrs({{7.0f, 8.0f, 9.0f}, rotation, {2.0f, 4.0f, 6.0f}});
    physics::detail::applyWorldPose(scene, body, target, false);
    CHECK(scene.get<scene::Transform>(body).rotation == rotation);
    CHECK(scene.get<scene::Transform>(body).scale == math::Vec3{1.0f, 2.0f, 3.0f});
    CHECK(scene.get<scene::Transform>(body).position == math::Vec3{2.0f, 2.0f, 2.0f});
    physics::detail::updateDescendants(scene, body, target);
    const math::Vec3 expected = math::Vec3(target * math::Vec4{0.0f, 1.0f, 0.0f, 1.0f});
    const math::Vec3 actual = math::Vec3(scene.get<scene::WorldTransform>(child).matrix[3]);
    CHECK(actual.x == Catch::Approx(expected.x));
    CHECK(actual.y == Catch::Approx(expected.y));
    CHECK(actual.z == Catch::Approx(expected.z));
    scene.updateTransforms();
    const auto restored = math::Vec3(scene.get<scene::WorldTransform>(body).matrix[3]);
    CHECK(restored.x == Catch::Approx(7.0f));
    CHECK(restored.y == Catch::Approx(8.0f));
    CHECK(restored.z == Catch::Approx(9.0f));
}

TEST_CASE("Body signatures ignore placement rounding and equivalent quaternion signs",
          "[physics][physics2d][signature]")
{
    const auto placed = [](float x)
    {
        physics::detail::Signature signature;
        signature.addPlacement(x);
        return signature.value();
    };
    CHECK(placed(1.0f) == placed(1.000001f));
    CHECK(placed(1.0f) != placed(1.001f));
    const auto turned = [](math::Quat rotation)
    {
        physics::detail::Signature signature;
        signature.addPlacement(rotation);
        return signature.value();
    };
    const math::Quat halfTurn{0.0f, 0.0f, 1.0f, 0.0f};
    CHECK(turned(halfTurn) == turned(-halfTurn));
    const math::Quat turn = math::angleAxis(0.25f, math::Vec3{0.0f, 0.0f, 1.0f});
    CHECK(turned(turn) == turned(-turn));
    CHECK(turned(turn) != turned(halfTurn));
}
