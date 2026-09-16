#include <devex/scene/Components.hpp>
#include <devex/scene/Scene.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using devex::math::Vec3;
using devex::scene::Entity;
using devex::scene::Scene;
using devex::scene::Transform;
using devex::scene::WorldTransform;

namespace {

struct Velocity
{
    Vec3 value{0.0f};
};

std::vector<std::string> childNames(const Scene& scene, Entity parent)
{
    std::vector<std::string> names;
    for (Entity child = parent.isValid() ? scene.firstChild(parent) : scene.firstRoot();
         child.isValid(); child = scene.nextSibling(child))
    {
        names.push_back(scene.name(child));
    }
    return names;
}

} // namespace

TEST_CASE("Entities have unique UUIDs and stale handles after destruction", "[scene]")
{
    Scene scene;
    const Entity first = scene.createEntity("First");
    const Entity second = scene.createEntity("Second");

    CHECK(scene.entityCount() == 2);
    CHECK(scene.uuid(first) != scene.uuid(second));
    CHECK(scene.findEntity(scene.uuid(second)) == second);

    const devex::core::Uuid firstUuid = scene.uuid(first);
    scene.destroyEntity(first);
    const Entity reused = scene.createEntity("Reused");

    CHECK_FALSE(scene.isAlive(first));
    CHECK(scene.isAlive(reused));
    CHECK(reused.index == first.index);
    CHECK_FALSE(scene.findEntity(firstUuid).isValid());
    CHECK(scene.entityCount() == 2);
}

TEST_CASE("Creating an entity with a used or nil UUID fails", "[scene]")
{
    Scene scene;
    const Entity entity = scene.createEntity();

    CHECK(scene.createEntity(scene.uuid(entity), "Duplicate").error().code ==
          devex::core::ErrorCode::AlreadyExists);
    CHECK(scene.createEntity(devex::core::Uuid{}, "Nil").error().code ==
          devex::core::ErrorCode::InvalidArgument);
}

TEST_CASE("Components are added, found and removed per entity", "[scene][components]")
{
    Scene scene;
    const Entity moving = scene.createEntity();
    const Entity still = scene.createEntity();

    scene.add<Transform>(moving, Transform{.position = {1.0f, 2.0f, 3.0f}});
    scene.add<Velocity>(moving, Vec3{0.0f, 0.0f, -1.0f});
    scene.add<Transform>(still);

    CHECK(scene.get<Transform>(moving).position == Vec3{1.0f, 2.0f, 3.0f});
    CHECK(scene.has<Velocity>(moving));
    CHECK_FALSE(scene.has<Velocity>(still));

    scene.remove<Transform>(moving);
    CHECK_FALSE(scene.has<Transform>(moving));
    CHECK(scene.has<Transform>(still));

    // Destroying an entity removes its components; the slot's next entity starts empty.
    scene.destroyEntity(moving);
    const Entity reused = scene.createEntity();
    CHECK_FALSE(scene.has<Velocity>(reused));
}

TEST_CASE("Views visit only entities that have every component", "[scene][view]")
{
    Scene scene;
    for (int index = 0; index < 6; ++index)
    {
        const Entity entity = scene.createEntity(std::to_string(index));
        scene.add<Transform>(entity);
        if (index % 2 == 0)
        {
            scene.add<Velocity>(entity, Vec3{static_cast<float>(index), 0.0f, 0.0f});
        }
    }

    int visited = 0;
    for (auto [entity, transform, velocity] : scene.view<Transform, Velocity>())
    {
        transform.position += velocity.value;
        CHECK(std::stoi(scene.name(entity)) % 2 == 0);
        ++visited;
    }
    CHECK(visited == 3);

    int unknown = 0;
    for ([[maybe_unused]] auto [entity, camera] : scene.view<devex::scene::Camera>())
    {
        ++unknown;
    }
    CHECK(unknown == 0);
}

TEST_CASE("Hierarchy keeps child order and rejects cycles", "[scene][hierarchy]")
{
    Scene scene;
    const Entity root = scene.createEntity("Root");
    const Entity a = scene.createEntity("A");
    const Entity b = scene.createEntity("B");
    const Entity c = scene.createEntity("C");

    REQUIRE(scene.setParent(a, root));
    REQUIRE(scene.setParent(b, root));
    REQUIRE(scene.setParent(c, a));

    CHECK(childNames(scene, root) == std::vector<std::string>{"A", "B"});
    CHECK(childNames(scene, Entity{}) == std::vector<std::string>{"Root"});
    CHECK(scene.parent(c) == a);
    CHECK_FALSE(scene.setParent(root, c).has_value());
    CHECK_FALSE(scene.setParent(a, a).has_value());

    REQUIRE(scene.setParent(a, Entity{}));
    CHECK(childNames(scene, root) == std::vector<std::string>{"B"});
    CHECK(childNames(scene, Entity{}) == std::vector<std::string>{"Root", "A"});

    // Destroying an entity destroys its descendants.
    scene.destroyEntity(a);
    CHECK_FALSE(scene.isAlive(c));
    CHECK(scene.entityCount() == 2);
}

TEST_CASE("Entities can be inserted before a sibling", "[scene][hierarchy]")
{
    Scene scene;
    const Entity root = scene.createEntity("Root");
    const Entity a = scene.createEntity("A");
    const Entity b = scene.createEntity("B");
    const Entity c = scene.createEntity("C");
    REQUIRE(scene.setParent(a, root));
    REQUIRE(scene.setParent(c, root));

    REQUIRE(scene.setParent(b, root, c));
    CHECK(childNames(scene, root) == std::vector<std::string>{"A", "B", "C"});

    REQUIRE(scene.setParent(c, root, a));
    CHECK(childNames(scene, root) == std::vector<std::string>{"C", "A", "B"});

    // The position must be a child of the new parent.
    CHECK_FALSE(scene.setParent(a, Entity{}, b).has_value());
    CHECK_FALSE(scene.setParent(a, root, a).has_value());
}

TEST_CASE("World transforms combine the ancestors' transforms", "[scene][hierarchy]")
{
    Scene scene;
    const Entity parent = scene.createEntity("Parent");
    const Entity group = scene.createEntity("Group without transform");
    const Entity child = scene.createEntity("Child");
    REQUIRE(scene.setParent(group, parent));
    REQUIRE(scene.setParent(child, group));

    scene.add<Transform>(parent, Transform{
                                     .position = {10.0f, 0.0f, 0.0f},
                                     .rotation = devex::math::angleAxis(devex::math::radians(90.0f),
                                                                        Vec3{0.0f, 1.0f, 0.0f}),
                                     .scale = Vec3{2.0f},
                                 });
    scene.add<Transform>(child, Transform{.position = {1.0f, 0.0f, 0.0f}});

    scene.updateTransforms();

    REQUIRE(scene.has<WorldTransform>(child));
    CHECK_FALSE(scene.has<WorldTransform>(group));
    // Scaled by 2, then turned towards -Z by the parent's rotation, then moved by its position.
    const devex::math::Vec4 origin = scene.get<WorldTransform>(child).matrix[3];
    CHECK_THAT(origin.x, WithinAbs(10.0, 1e-5));
    CHECK_THAT(origin.y, WithinAbs(0.0, 1e-5));
    CHECK_THAT(origin.z, WithinAbs(-2.0, 1e-5));
}
