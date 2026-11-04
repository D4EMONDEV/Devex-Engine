#include <devex/net/Multiplayer.hpp>
#include <devex/runtime/PathFollowers.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/NetworkComponents.hpp>
#include <devex/scene/PathComponents.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using devex::math::Vec3;
using devex::runtime::PathFollowers;
using devex::scene::Entity;
using devex::scene::Path3D;
using devex::scene::PathCurve;
using devex::scene::PathEnd;
using devex::scene::PathFollower;
using devex::scene::Scene;
using devex::scene::Transform;

namespace {

// Two points 10 meters apart along X.
[[nodiscard]] Path3D straight()
{
    return Path3D{.points = {{0.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}}, .handlesIn = {}, .handlesOut = {}, .closed = false};
}

void updateTransforms(Scene& scene, Entity entity)
{
    auto& world = scene.has<devex::scene::WorldTransform>(entity) ? scene.get<devex::scene::WorldTransform>(entity) : scene.add<devex::scene::WorldTransform>(entity);
    world.matrix = scene.get<Transform>(entity).matrix();
}

} // namespace

TEST_CASE("A curve tells where a distance along it is", "[runtime][path]")
{
    const PathCurve line(straight());
    CHECK(line.length() == Approx(10.0f));
    CHECK(line.positionAt(2.5f).x == Approx(2.5f));
    CHECK(line.directionAt(2.5f).x == Approx(1.0f));
    // Past its ends, it stays at them.
    CHECK(line.positionAt(-3.0f).x == Approx(0.0f));
    CHECK(line.positionAt(30.0f).x == Approx(10.0f));
    CHECK(line.closestDistance({4.0f, 5.0f, 0.0f}) == Approx(4.0f));

    // Without anything, or with one point, it is there, of no length.
    CHECK(PathCurve(Path3D{}).empty());
    const PathCurve single(Path3D{.points = {{1.0f, 2.0f, 3.0f}}, .handlesIn = {}, .handlesOut = {}, .closed = true});
    CHECK(single.length() == 0.0f);
    CHECK(single.positionAt(5.0f).y == 2.0f);
    CHECK_FALSE(single.closed());
}

TEST_CASE("Handles shape a curve between its points, which it goes through", "[runtime][path]")
{
    Path3D bent = straight();
    bent.handlesOut = {{0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f}};
    bent.handlesIn = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 5.0f}};
    const PathCurve curve(bent);
    CHECK(curve.length() > 10.5f);
    CHECK(curve.positionAt(0.0f).x == Approx(0.0f));
    CHECK(curve.positionAt(curve.length()).x == Approx(10.0f));
    // The middle goes out by three quarters of its handles, and it leaves the way they say.
    CHECK(curve.positionAt(curve.length() * 0.5f).z == Approx(3.75f).margin(0.05f));
    CHECK(curve.directionAt(0.0f).z > 0.5f);
    // Handles the file does not have count as none.
    Path3D partial = straight();
    partial.handlesOut = {{0.0f, 0.0f, 5.0f}};
    CHECK(PathCurve(partial).length() > 10.0f);
}

TEST_CASE("A closed curve goes around", "[runtime][path]")
{
    Path3D square{.points = {{0.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 4.0f}, {0.0f, 0.0f, 4.0f}}, .handlesIn = {}, .handlesOut = {}, .closed = true};
    const PathCurve around(square);
    CHECK(around.closed());
    CHECK(around.length() == Approx(16.0f));
    CHECK(around.positionAt(17.0f).x == Approx(1.0f));
    CHECK(around.positionAt(-1.0f).z == Approx(1.0f));
    CHECK(around.positionAt(-1.0f).x == Approx(0.0f));
    square.closed = false;
    CHECK(PathCurve(square).length() == Approx(12.0f));
}

TEST_CASE("A follower moves along the path of its parent, as it was told to end", "[runtime][path]")
{
    Scene scene;
    const Entity rail = scene.createEntity("Rail");
    scene.add<Transform>(rail);
    scene.add<Path3D>(rail) = straight();
    const Entity cart = scene.createEntity("Cart");
    scene.add<Transform>(cart);
    REQUIRE(scene.setParent(cart, rail));
    auto& follower = scene.add<PathFollower>(cart);
    follower.speed = 4.0f;
    PathFollowers followers;
    const auto step = [&](double seconds) { followers.update(scene, devex::core::Duration(seconds), nullptr); };
    const auto x = [&] { return scene.get<Transform>(cart).position.x; };

    SECTION("Around")
    {
        step(1.0);
        CHECK(x() == Approx(4.0f));
        // It looks where it goes: its -Z along +X.
        CHECK((scene.get<Transform>(cart).rotation * Vec3{0.0f, 0.0f, -1.0f}).x == Approx(1.0f));
        step(2.0);
        CHECK(x() == Approx(2.0f));
    }
    SECTION("Stopping at the end")
    {
        follower.end = PathEnd::Stop;
        step(3.0);
        CHECK(x() == Approx(10.0f));
        CHECK_FALSE(follower.playing);
        step(1.0);
        CHECK(x() == Approx(10.0f));
        // Going again from elsewhere.
        follower.distance = 1.0f;
        follower.playing = true;
        step(1.0);
        CHECK(x() == Approx(5.0f));
    }
    SECTION("Back and forth")
    {
        follower.end = PathEnd::PingPong;
        step(3.0);
        CHECK(x() == Approx(8.0f));
        CHECK(follower.reversed);
        CHECK((scene.get<Transform>(cart).rotation * Vec3{0.0f, 0.0f, -1.0f}).x == Approx(-1.0f));
        step(2.5);
        CHECK(x() == Approx(2.0f));
        CHECK_FALSE(follower.reversed);
    }
    SECTION("Told to wait, and where to be")
    {
        follower.playing = false;
        follower.distance = 7.0f;
        step(1.0);
        CHECK(x() == Approx(7.0f));
    }
    SECTION("Keeping its rotation")
    {
        follower.orient = false;
        step(1.0);
        CHECK(scene.get<Transform>(cart).rotation.w == 1.0f);
    }
    SECTION("A path that changes moves it at once")
    {
        step(1.0);
        scene.get<Path3D>(rail).points[1] = {0.0f, 10.0f, 0.0f};
        step(0.0);
        CHECK(scene.get<Transform>(cart).position.y == Approx(4.0f));
    }
}

TEST_CASE("A follower can follow a path elsewhere, in the world", "[runtime][path]")
{
    Scene scene;
    const Entity rail = scene.createEntity("Rail");
    scene.add<Transform>(rail).position = {100.0f, 0.0f, 0.0f};
    scene.add<Path3D>(rail) = straight();
    updateTransforms(scene, rail);
    const Entity holder = scene.createEntity("Holder");
    scene.add<Transform>(holder).position = {0.0f, 5.0f, 0.0f};
    updateTransforms(scene, holder);
    const Entity cart = scene.createEntity("Cart");
    scene.add<Transform>(cart);
    REQUIRE(scene.setParent(cart, holder));
    auto& follower = scene.add<PathFollower>(cart);
    follower.path = devex::scene::EntityRef{scene.uuid(rail)};
    follower.speed = 1.0f;
    PathFollowers followers;
    followers.update(scene, devex::core::Duration(3.0), nullptr);
    // At the place of the world on the rail, through the transform of what holds it.
    CHECK(scene.get<Transform>(cart).position.x == Approx(103.0f));
    CHECK(scene.get<Transform>(cart).position.y == Approx(-5.0f));

    // Without a path anywhere, it stays.
    follower.path = devex::scene::EntityRef{};
    followers.update(scene, devex::core::Duration(1.0), nullptr);
    CHECK(follower.distance == 3.0f);
}

TEST_CASE("A follower is left alone when another player moves it", "[runtime][path][net]")
{
    Scene scene;
    const Entity rail = scene.createEntity("Rail");
    scene.add<Transform>(rail);
    scene.add<Path3D>(rail) = straight();
    const Entity cart = scene.createEntity("Cart");
    scene.add<Transform>(cart);
    REQUIRE(scene.setParent(cart, rail));
    scene.add<PathFollower>(cart).speed = 1.0f;
    scene.add<devex::scene::NetworkSync>(cart);
    devex::net::Multiplayer session;
    PathFollowers followers;
    // Alone, this game decides.
    followers.update(scene, devex::core::Duration(1.0), &session);
    CHECK(scene.get<Transform>(cart).position.x == Approx(1.0f));
    REQUIRE(session.host(devex::net::findFreePort()));
    session.setAuthority(scene.uuid(cart), 2);
    followers.update(scene, devex::core::Duration(1.0), &session);
    CHECK(scene.get<Transform>(cart).position.x == Approx(1.0f));
}

TEST_CASE("What a path tells is in the coordinates of the world", "[runtime][path]")
{
    Scene scene;
    const Entity rail = scene.createEntity("Rail");
    auto& transform = scene.add<Transform>(rail);
    transform.position = {0.0f, 3.0f, 0.0f};
    transform.scale = Vec3{2.0f};
    scene.add<Path3D>(rail) = straight();
    updateTransforms(scene, rail);
    CHECK(devex::runtime::pathLength(scene, rail) == Approx(20.0f));
    CHECK(devex::runtime::pathPositionAt(scene, rail, 5.0f).x == Approx(10.0f));
    CHECK(devex::runtime::pathPositionAt(scene, rail, 5.0f).y == Approx(3.0f));
    CHECK(devex::runtime::pathDirectionAt(scene, rail, 5.0f).x == Approx(1.0f));
    CHECK(devex::runtime::pathClosestDistance(scene, rail, {8.0f, 9.0f, 0.0f}) == Approx(4.0f));
    const Entity nothing = scene.createEntity("Nothing");
    CHECK(devex::runtime::pathLength(scene, nothing) == 0.0f);
}

TEST_CASE("A path and its follower are kept with the scene", "[runtime][path]")
{
    Scene scene;
    const Entity rail = scene.createEntity("Rail");
    scene.add<Transform>(rail);
    auto& path = scene.add<Path3D>(rail);
    path.points = {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}};
    path.handlesIn = {{0.5f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.0f}};
    path.handlesOut = {{0.0f, 0.0f, 0.5f}, {0.25f, 0.0f, 0.0f}};
    path.closed = true;
    auto& follower = scene.add<PathFollower>(rail);
    follower.end = PathEnd::PingPong;
    follower.speed = 3.5f;
    follower.reversed = true;
    const auto loaded = devex::scene::loadScene(devex::scene::saveScene(scene));
    REQUIRE(loaded.has_value());
    const Entity again = loaded->findEntity(scene.uuid(rail));
    const Path3D& read = loaded->get<Path3D>(again);
    REQUIRE(read.points.size() == 2);
    CHECK(read.points[1].z == 6.0f);
    CHECK(read.handlesIn[1].y == 0.5f);
    CHECK(read.handlesOut[0].z == 0.5f);
    CHECK(read.closed);
    CHECK(loaded->get<PathFollower>(again).end == PathEnd::PingPong);
    CHECK(loaded->get<PathFollower>(again).speed == 3.5f);
    // Where it goes back from is the game's, not the scene's.
    CHECK_FALSE(loaded->get<PathFollower>(again).reversed);
}
