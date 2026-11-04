#include <devex/csg/CsgBuilder.hpp>
#include <devex/navigation/NavMeshBuilder.hpp>
#include <devex/physics/PhysicsWorld.hpp>
#include <devex/runtime/AssetManager.hpp>
#include <devex/runtime/CsgWorld.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/CsgComponents.hpp>
#include <devex/scene/PathComponents.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using Catch::Approx;
using devex::asset::AssetId;
using devex::asset::MeshData;
using devex::core::Uuid;
using devex::csg::Node;
using devex::csg::Operation;
using devex::csg::Shape;
using devex::math::Vec3;
using devex::runtime::AssetManager;
using devex::runtime::CsgWorld;
using devex::scene::CsgBox;
using devex::scene::CsgOperation;
using devex::scene::CsgResult;
using devex::scene::Entity;
using devex::scene::Scene;
using devex::scene::Transform;

namespace {

[[nodiscard]] float volumeOf(const MeshData& mesh)
{
    float volume = 0.0f;
    for (std::size_t index = 0; index + 2 < mesh.indices.size(); index += 3)
    {
        const Vec3 a = mesh.vertices[mesh.indices[index]].position;
        const Vec3 b = mesh.vertices[mesh.indices[index + 1]].position;
        const Vec3 c = mesh.vertices[mesh.indices[index + 2]].position;
        volume += devex::math::dot(a, devex::math::cross(b, c)) / 6.0f;
    }
    return volume;
}

[[nodiscard]] Node box(Vec3 size, Vec3 at = Vec3{0.0f}, Operation operation = Operation::Union)
{
    Node node;
    node.shape = Shape::Box;
    node.size = size;
    node.operation = operation;
    node.transform = devex::math::translate(devex::math::Mat4{1.0f}, at);
    return node;
}

[[nodiscard]] MeshData built(const Node& node)
{
    auto mesh = devex::csg::buildMesh(node);
    REQUIRE(mesh.has_value());
    return *std::move(mesh);
}

[[nodiscard]] float highest(const MeshData& mesh, float Vec3::*axis)
{
    float top = -1.0e9f;
    for (const auto& vertex : mesh.vertices)
    {
        top = std::max(top, vertex.position.*axis);
    }
    return top;
}

const AssetId red{Uuid::fromParts(1, 1)};
const AssetId blue{Uuid::fromParts(2, 2)};

} // namespace

TEST_CASE("A box is a solid with hard edges, seen from outside", "[csg]")
{
    const MeshData mesh = built(box({1.0f, 2.0f, 3.0f}));
    CHECK(volumeOf(mesh) == Approx(6.0f));
    // Four corners for each of its six faces, whose normals do not meet.
    CHECK(mesh.vertices.size() == 24);
    CHECK(mesh.indices.size() == 36);
    for (const auto& vertex : mesh.vertices)
    {
        const float out = std::max({std::abs(vertex.normal.x), std::abs(vertex.normal.y), std::abs(vertex.normal.z)});
        CHECK(out == Approx(1.0f));
        // On the outside: the normal goes the way of the corner.
        CHECK(devex::math::dot(vertex.normal, vertex.position) > 0.0f);
        CHECK(std::abs(devex::math::dot(vertex.normal, Vec3(vertex.tangent))) < 1.0e-4f);
    }
}

TEST_CASE("Solids add up, cut into each other and keep what they share", "[csg]")
{
    Node root = box({2.0f, 2.0f, 2.0f});
    SECTION("Added, what overlaps counts once")
    {
        root.children.push_back(box({2.0f, 2.0f, 2.0f}, {1.0f, 0.0f, 0.0f}));
        CHECK(volumeOf(built(root)) == Approx(12.0f));
    }
    SECTION("Cut out")
    {
        root.children.push_back(box({1.0f, 4.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, Operation::Subtract));
        CHECK(volumeOf(built(root)) == Approx(6.0f));
    }
    SECTION("Only what they share")
    {
        root.children.push_back(box({2.0f, 2.0f, 2.0f}, {1.0f, 0.0f, 0.0f}, Operation::Intersect));
        CHECK(volumeOf(built(root)) == Approx(4.0f));
    }
    SECTION("In order, each on what came before")
    {
        root.children.push_back(box({1.0f, 4.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, Operation::Subtract));
        root.children.push_back(box({1.0f, 1.0f, 1.0f}));
        CHECK(volumeOf(built(root)) == Approx(7.0f));
    }
    SECTION("A group makes its result first, then cuts as one")
    {
        Node group;
        group.operation = Operation::Subtract;
        group.children.push_back(box({1.0f, 4.0f, 1.0f}, {-0.5f, 0.0f, 0.0f}));
        group.children.push_back(box({1.0f, 4.0f, 1.0f}, {0.5f, 0.0f, 0.0f}));
        root.children.push_back(group);
        CHECK(volumeOf(built(root)) == Approx(4.0f));
    }
    SECTION("Nothing is left, or nothing was there")
    {
        root.children.push_back(box({4.0f, 4.0f, 4.0f}, {0.0f, 0.0f, 0.0f}, Operation::Subtract));
        CHECK(built(root).vertices.empty());
        CHECK(built(Node{}).vertices.empty());
        CHECK(built(box({0.0f, 1.0f, 1.0f})).vertices.empty());
    }
}

TEST_CASE("Spheres and cylinders are round, and stand up along Y", "[csg]")
{
    Node sphere;
    sphere.shape = Shape::Sphere;
    sphere.radius = 2.0f;
    sphere.sides = 48;
    const MeshData ball = built(sphere);
    CHECK(volumeOf(ball) == Approx(4.0f / 3.0f * std::numbers::pi_v<float> * 8.0f).epsilon(0.03));
    // Smooth: its normals go out from its center, and its faces share their corners.
    CHECK(devex::math::dot(ball.vertices.front().normal, devex::math::normalize(ball.vertices.front().position)) > 0.99f);
    CHECK(ball.vertices.size() * 3 < ball.indices.size());

    Node cylinder;
    cylinder.shape = Shape::Cylinder;
    cylinder.radius = 1.0f;
    cylinder.height = 6.0f;
    cylinder.sides = 48;
    const MeshData upright = built(cylinder);
    CHECK(volumeOf(upright) == Approx(std::numbers::pi_v<float> * 6.0f).epsilon(0.02));
    CHECK(highest(upright, &Vec3::y) == Approx(3.0f));
    CHECK(highest(upright, &Vec3::x) == Approx(1.0f));
    CHECK(highest(upright, &Vec3::z) == Approx(1.0f));
}

TEST_CASE("Each face keeps the material of the shape it comes from", "[csg]")
{
    Node root = box({2.0f, 2.0f, 2.0f});
    root.material = red;
    root.children.push_back(box({1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, Operation::Subtract));
    root.children.back().material = blue;
    const MeshData mesh = built(root);
    REQUIRE(mesh.submeshes.size() == 2);
    CHECK(mesh.submeshes[0].material != mesh.submeshes[1].material);
    std::uint32_t covered = 0;
    for (const auto& submesh : mesh.submeshes)
    {
        CHECK((submesh.material == red || submesh.material == blue));
        CHECK(submesh.firstIndex == covered);
        covered += submesh.indexCount;
        // What the cutter leaves is on the inside of the notch, in its own material.
        if (submesh.material == blue)
        {
            for (std::uint32_t index = submesh.firstIndex; index < submesh.firstIndex + submesh.indexCount; ++index)
            {
                const Vec3 position = mesh.vertices[mesh.indices[index]].position;
                CHECK(std::min({position.x, position.y, position.z}) >= -0.001f);
            }
        }
    }
    CHECK(covered == mesh.indices.size());
}

TEST_CASE("A band goes along a line, its top on it", "[csg]")
{
    Node road;
    road.shape = Shape::Extrusion;
    road.width = 2.0f;
    road.depth = 0.5f;
    SECTION("Open, with its ends closed")
    {
        road.line = {{0.0f, 1.0f, 0.0f}, {5.0f, 1.0f, 0.0f}, {10.0f, 1.0f, 0.0f}};
        const MeshData mesh = built(road);
        CHECK(volumeOf(mesh) == Approx(10.0f));
        CHECK(highest(mesh, &Vec3::y) == Approx(1.0f));
        CHECK(highest(mesh, &Vec3::x) == Approx(10.0f));
        CHECK(highest(mesh, &Vec3::z) == Approx(1.0f));
    }
    SECTION("Around, without ends")
    {
        road.line = {{0.0f, 0.0f, 0.0f}, {20.0f, 0.0f, 0.0f}, {20.0f, 0.0f, 20.0f}, {0.0f, 0.0f, 20.0f}};
        road.closed = true;
        // The width stays in the corners: a square ring, 22 meters outside and 18 inside.
        CHECK(volumeOf(built(road)) == Approx((22.0f * 22.0f - 18.0f * 18.0f) * 0.5f));
    }
    SECTION("A hole can be cut in it")
    {
        road.line = {{0.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}};
        road.children.push_back(box({2.0f, 4.0f, 4.0f}, {5.0f, 0.0f, 0.0f}, Operation::Subtract));
        CHECK(volumeOf(built(road)) == Approx(8.0f));
    }
    SECTION("Too short to go anywhere")
    {
        road.line = {{1.0f, 2.0f, 3.0f}};
        CHECK(built(road).vertices.empty());
        road.line = {{1.0f, 2.0f, 3.0f}, {1.0f, 2.0f, 3.0f}};
        CHECK(built(road).vertices.empty());
    }
}

namespace {

struct World
{
    Scene scene;
    AssetManager assets{nullptr, nullptr};
    CsgWorld csg;

    [[nodiscard]] Entity shape(std::string_view name, Vec3 size, Vec3 at = Vec3{0.0f}, Entity parent = {}, CsgOperation operation = CsgOperation::Union)
    {
        const Entity entity = scene.createEntity(std::string(name));
        auto& transform = scene.add<Transform>(entity);
        transform.position = at;
        auto& box = scene.add<CsgBox>(entity);
        box.size = size;
        box.operation = operation;
        if (parent.isValid())
        {
            REQUIRE(scene.setParent(entity, parent));
        }
        return entity;
    }

    [[nodiscard]] const MeshData* result(Entity root)
    {
        const CsgResult* const solid = scene.tryGet<CsgResult>(root);
        return solid != nullptr && solid->mesh.isValid() ? assets.meshData(solid->mesh) : nullptr;
    }
};

} // namespace

TEST_CASE("The root of the shapes of a scene gets one mesh, made again when they change", "[csg]")
{
    World world;
    const Entity root = world.shape("Room", {4.0f, 4.0f, 4.0f});
    const Entity door = world.shape("Door", {1.0f, 2.0f, 8.0f}, {0.0f, -1.0f, 0.0f}, root, CsgOperation::Subtract);
    world.csg.update(world.scene, world.assets);
    REQUIRE(world.result(root) != nullptr);
    CHECK(volumeOf(*world.result(root)) == Approx(64.0f - 8.0f));
    // Only the root has a result.
    CHECK_FALSE(world.scene.has<CsgResult>(door));
    const AssetId first = world.scene.get<CsgResult>(root).mesh;

    // Nothing changes: the same mesh.
    world.csg.update(world.scene, world.assets);
    CHECK(world.scene.get<CsgResult>(root).mesh == first);
    // Moving the solid moves its mesh, which stays.
    world.scene.get<Transform>(root).position = {10.0f, 0.0f, 0.0f};
    world.csg.update(world.scene, world.assets);
    CHECK(world.scene.get<CsgResult>(root).mesh == first);

    // What it is made of changes, by moving a part or by changing it: another mesh, and the old one goes.
    world.scene.get<CsgBox>(door).size.x = 2.0f;
    world.csg.update(world.scene, world.assets);
    const AssetId second = world.scene.get<CsgResult>(root).mesh;
    CHECK(second != first);
    CHECK(world.assets.meshData(first) == nullptr);
    CHECK(volumeOf(*world.result(root)) == Approx(64.0f - 16.0f));
    world.scene.get<Transform>(door).position.x = 100.0f;
    world.csg.update(world.scene, world.assets);
    CHECK(volumeOf(*world.result(root)) == Approx(64.0f));

    // A part that goes takes its effect with it.
    world.scene.get<Transform>(door).position.x = 0.0f;
    world.scene.destroyEntity(door);
    world.csg.update(world.scene, world.assets);
    CHECK(volumeOf(*world.result(root)) == Approx(64.0f));

    // And the mesh goes with the last of them.
    const AssetId last = world.scene.get<CsgResult>(root).mesh;
    world.scene.destroyEntity(root);
    world.csg.update(world.scene, world.assets);
    CHECK(world.assets.meshData(last) == nullptr);
}

TEST_CASE("What is a root of shapes depends on what is above it", "[csg]")
{
    World world;
    const Entity plain = world.scene.createEntity("Group");
    world.scene.add<Transform>(plain);
    // Under something that is not a shape, it is a solid of its own.
    const Entity alone = world.shape("Alone", {1.0f, 1.0f, 1.0f}, Vec3{0.0f}, plain);
    const Entity root = world.shape("Root", {2.0f, 2.0f, 2.0f});
    // What is under a plain entity under a solid is not part of it.
    const Entity between = world.scene.createEntity("Between");
    world.scene.add<Transform>(between);
    REQUIRE(world.scene.setParent(between, root));
    const Entity far = world.shape("Far", {3.0f, 3.0f, 3.0f}, {10.0f, 0.0f, 0.0f}, between);
    world.csg.update(world.scene, world.assets);
    CHECK(volumeOf(*world.result(alone)) == Approx(1.0f));
    CHECK(volumeOf(*world.result(root)) == Approx(8.0f));
    CHECK(volumeOf(*world.result(far)) == Approx(27.0f));

    // Moved under a solid, it no longer is one.
    REQUIRE(world.scene.setParent(alone, root));
    world.csg.update(world.scene, world.assets);
    CHECK_FALSE(world.scene.has<CsgResult>(alone));
    CHECK(volumeOf(*world.result(root)) == Approx(8.0f));

    // A shape taken away from an entity leaves nothing behind.
    world.scene.remove<CsgBox>(far);
    world.csg.update(world.scene, world.assets);
    CHECK_FALSE(world.scene.has<CsgResult>(far));
}

TEST_CASE("A band follows its path, in the space of its root", "[csg]")
{
    World world;
    const Entity rail = world.scene.createEntity("Rail");
    auto& transform = world.scene.add<Transform>(rail);
    transform.position = {5.0f, 0.0f, 0.0f};
    world.scene.add<devex::scene::WorldTransform>(rail).matrix = transform.matrix();
    world.scene.add<devex::scene::Path3D>(rail).points = {{0.0f, 2.0f, 0.0f}, {10.0f, 2.0f, 0.0f}};
    const Entity road = world.scene.createEntity("Road");
    world.scene.add<Transform>(road);
    world.scene.add<devex::scene::WorldTransform>(road);
    auto& band = world.scene.add<devex::scene::CsgPath>(road);
    band.path = devex::scene::EntityRef{world.scene.uuid(rail)};
    band.width = 2.0f;
    band.depth = 0.5f;
    world.csg.update(world.scene, world.assets);
    const MeshData* mesh = world.result(road);
    REQUIRE(mesh != nullptr);
    CHECK(volumeOf(*mesh) == Approx(10.0f));
    CHECK(highest(*mesh, &Vec3::x) == Approx(15.0f));
    CHECK(highest(*mesh, &Vec3::y) == Approx(2.0f));

    // A curve that changes changes it.
    world.scene.get<devex::scene::Path3D>(rail).points[1].x = 20.0f;
    world.csg.update(world.scene, world.assets);
    CHECK(volumeOf(*world.result(road)) == Approx(20.0f));

    // A child of its path needs no reference, and without one it is nothing.
    band.path = {};
    world.csg.update(world.scene, world.assets);
    CHECK_FALSE(world.scene.get<CsgResult>(road).mesh.isValid());
    REQUIRE(world.scene.setParent(road, rail));
    world.csg.update(world.scene, world.assets);
    CHECK(volumeOf(*world.result(road)) == Approx(20.0f));
}

TEST_CASE("A solid collides with what it is, holes included, unless told otherwise", "[csg]")
{
    World world;
    const Entity wall = world.shape("Wall", {10.0f, 10.0f, 1.0f});
    (void)world.shape("Hole", {2.0f, 2.0f, 4.0f}, Vec3{0.0f}, wall, CsgOperation::Subtract);
    world.scene.add<devex::scene::WorldTransform>(wall);
    world.csg.update(world.scene, world.assets);
    auto physics = devex::physics::PhysicsWorld::create({.settings = {}, .meshes = [&](AssetId id) { return world.assets.meshData(id); }, .threads = 1});
    REQUIRE(physics.has_value());
    (*physics)->step(world.scene, devex::core::Duration(1.0 / 60.0));
    const Vec3 forward{0.0f, 0.0f, -1.0f};
    CHECK((*physics)->raycast({3.0f, 0.0f, 5.0f}, forward, 10.0f).has_value());
    CHECK_FALSE((*physics)->raycast({0.0f, 0.0f, 5.0f}, forward, 10.0f).has_value());

    // Without a collider, nothing stops there. Moving it back changes nothing to its mesh.
    world.scene.get<CsgBox>(wall).collision = false;
    world.csg.update(world.scene, world.assets);
    (*physics)->step(world.scene, devex::core::Duration(1.0 / 60.0));
    CHECK_FALSE((*physics)->raycast({3.0f, 0.0f, 5.0f}, forward, 10.0f).has_value());
}

TEST_CASE("Navigation walks on a solid", "[csg]")
{
    World world;
    const Entity floor = world.shape("Floor", {20.0f, 1.0f, 20.0f});
    world.scene.add<devex::scene::WorldTransform>(floor);
    world.csg.update(world.scene, world.assets);
    const auto geometry = devex::navigation::collectGeometry(world.scene, [&](AssetId id) { return world.assets.meshData(id); });
    CHECK(geometry.vertices.size() == 24);
    CHECK(geometry.indices.size() == 36);
}

TEST_CASE("Shapes are kept with the scene, and what they make comes back", "[csg]")
{
    World world;
    const Entity root = world.shape("Room", {4.0f, 4.0f, 4.0f});
    world.scene.get<CsgBox>(root).collision = false;
    (void)world.shape("Hole", {1.0f, 8.0f, 1.0f}, Vec3{0.0f}, root, CsgOperation::Subtract);
    world.csg.update(world.scene, world.assets);
    const std::string text = devex::scene::saveScene(world.scene);
    CHECK(text.find("CsgResult") == std::string::npos);
    auto loaded = devex::scene::loadScene(text);
    REQUIRE(loaded.has_value());
    CsgWorld again;
    AssetManager assets{nullptr, nullptr};
    again.update(*loaded, assets);
    const Entity read = loaded->findEntity(world.scene.uuid(root));
    CHECK_FALSE(loaded->get<CsgBox>(read).collision);
    CHECK(volumeOf(*assets.meshData(loaded->get<CsgResult>(read).mesh)) == Approx(64.0f - 4.0f));
}
