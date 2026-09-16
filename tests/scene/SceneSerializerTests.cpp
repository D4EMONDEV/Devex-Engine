#include <devex/asset/AssetId.hpp>
#include <devex/core/Log.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using devex::math::Quat;
using devex::math::Vec3;
using devex::scene::Camera;
using devex::scene::Entity;
using devex::scene::MeshRenderer;
using devex::scene::Scene;
using devex::scene::Transform;

namespace test {

struct Tag
{
    std::string label;
    std::uint32_t priority = 0;
    bool enabled = true;
};
DEVEX_DECLARE_REFLECTION(Tag);

DEVEX_REFLECT(Tag)
{
    type.field("label", &Tag::label)
        .field("priority", &Tag::priority)
        .field("enabled", &Tag::enabled);
}

} // namespace test

namespace {

// Collects warnings, such as those about skipped components.
class WarningCapture
{
public:
    WarningCapture()
        : m_sink(devex::core::addLogSink([this](const devex::core::LogRecord& record) {
            if (record.level == devex::core::LogLevel::Warning)
            {
                m_warnings.emplace_back(record.message);
            }
        }))
    {
    }

    ~WarningCapture()
    {
        devex::core::removeLogSink(m_sink);
    }

    WarningCapture(const WarningCapture&) = delete;
    WarningCapture& operator=(const WarningCapture&) = delete;

    [[nodiscard]] const std::vector<std::string>& warnings() const noexcept
    {
        return m_warnings;
    }

private:
    std::vector<std::string> m_warnings;
    devex::core::LogSinkId m_sink;
};

} // namespace

TEST_CASE("Saved scenes load back with UUIDs, hierarchy and components", "[scene][serializer]")
{
    devex::scene::registerComponent<test::Tag>();

    Scene original;
    const Entity world = original.createEntity("World");
    const Entity player = original.createEntity("Player \"One\"");
    const Entity weapon = original.createEntity("Weapon");
    const Entity camera = original.createEntity("Camera");
    REQUIRE(original.setParent(player, world));
    REQUIRE(original.setParent(camera, world));
    REQUIRE(original.setParent(weapon, player));

    const Quat rotation = devex::math::angleAxis(0.3f, devex::math::normalize(Vec3{1.0f, 1.0f, 0.0f}));
    original.add<Transform>(player, Transform{
                                        .position = {0.1f, 1.5f, -2.0f},
                                        .rotation = rotation,
                                        .scale = {1.0f, 2.0f, 3.0f},
                                    });
    original.add<MeshRenderer>(weapon, devex::asset::builtin::cubeMesh);
    original.add<Camera>(camera, Camera{.verticalFov = 1.2f, .nearPlane = 0.05f, .primary = false});
    original.add<test::Tag>(player, test::Tag{.label = "hero", .priority = 7, .enabled = false});

    const std::string text = devex::scene::saveScene(original);
    INFO(text);
    auto loaded = devex::scene::loadScene(text);
    REQUIRE(loaded.has_value());

    CHECK(loaded->entityCount() == 4);
    const Entity loadedPlayer = loaded->findEntity(original.uuid(player));
    const Entity loadedWorld = loaded->findEntity(original.uuid(world));
    const Entity loadedWeapon = loaded->findEntity(original.uuid(weapon));
    const Entity loadedCamera = loaded->findEntity(original.uuid(camera));
    REQUIRE(loadedPlayer.isValid());
    CHECK(loaded->name(loadedPlayer) == "Player \"One\"");
    CHECK(loaded->parent(loadedPlayer) == loadedWorld);
    CHECK(loaded->parent(loadedWeapon) == loadedPlayer);
    CHECK(loaded->firstChild(loadedWorld) == loadedPlayer);
    CHECK(loaded->nextSibling(loadedPlayer) == loadedCamera);

    const Transform& transform = loaded->get<Transform>(loadedPlayer);
    CHECK(transform.position == Vec3{0.1f, 1.5f, -2.0f});
    CHECK(transform.rotation == rotation);
    CHECK(transform.scale == Vec3{1.0f, 2.0f, 3.0f});
    CHECK(loaded->get<MeshRenderer>(loadedWeapon).mesh == devex::asset::builtin::cubeMesh);
    CHECK(loaded->get<Camera>(loadedCamera).nearPlane == 0.05f);
    CHECK_FALSE(loaded->get<Camera>(loadedCamera).primary);
    CHECK(loaded->get<test::Tag>(loadedPlayer).label == "hero");
    CHECK(loaded->get<test::Tag>(loadedPlayer).priority == 7);

    // Saving the loaded scene gives the same text.
    CHECK(devex::scene::saveScene(*loaded) == text);
}

TEST_CASE("Scene files use the documented format", "[scene][serializer]")
{
    Scene scene;
    const Entity entity = scene.createEntity(
        devex::core::Uuid::parse("6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23").value(), "Cube").value();
    scene.add<Transform>(entity, Transform{.position = {0.0f, 1.0f, 0.0f}});
    scene.add<MeshRenderer>(entity, devex::asset::builtin::cubeMesh);

    CHECK(devex::scene::saveScene(scene) == R"([scene format=1]

[entity uuid="6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23" name="Cube"]

[component type="Transform"]
position = vec3(0, 1, 0)
rotation = quat(0, 0, 0, 1)
scale = vec3(1, 1, 1)

[component type="MeshRenderer"]
mesh = asset("00000000-0000-0000-0000-000000000001")
)");
}

TEST_CASE("Entity trees are restored with their UUIDs and position", "[scene][serializer]")
{
    Scene scene;
    const Entity root = scene.createEntity("Root");
    const Entity first = scene.createEntity("First");
    const Entity branch = scene.createEntity("Branch");
    const Entity leaf = scene.createEntity("Leaf");
    const Entity last = scene.createEntity("Last");
    REQUIRE(scene.setParent(first, root));
    REQUIRE(scene.setParent(branch, root));
    REQUIRE(scene.setParent(last, root));
    REQUIRE(scene.setParent(leaf, branch));
    scene.add<Transform>(leaf, Transform{.position = {4.0f, 5.0f, 6.0f}});

    const devex::core::Uuid branchUuid = scene.uuid(branch);
    const devex::core::Uuid leafUuid = scene.uuid(leaf);
    const std::string snapshot = devex::scene::saveEntityTree(scene, branch);
    scene.destroyEntity(branch);
    REQUIRE(scene.entityCount() == 3);

    const auto restored = devex::scene::loadEntityTree(scene, snapshot, root, last);
    REQUIRE(restored.has_value());

    CHECK(scene.uuid(*restored) == branchUuid);
    CHECK(scene.nextSibling(first) == *restored);
    CHECK(scene.nextSibling(*restored) == last);
    const Entity restoredLeaf = scene.findEntity(leafUuid);
    REQUIRE(restoredLeaf.isValid());
    CHECK(scene.parent(restoredLeaf) == *restored);
    CHECK(scene.get<Transform>(restoredLeaf).position == Vec3{4.0f, 5.0f, 6.0f});

    // Loading the same tree again fails because its UUIDs are taken, and creates nothing.
    CHECK_FALSE(devex::scene::loadEntityTree(scene, snapshot, Entity{}).has_value());
    CHECK(scene.entityCount() == 5);
}

TEST_CASE("Unknown components and fields are skipped with warnings", "[scene][serializer]")
{
    const WarningCapture capture;
    const auto scene = devex::scene::loadScene(R"([scene format=1]
[entity uuid="6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23" name="Future"]
[component type="Hologram"]
brightness = 3
[component type="Transform"]
position = vec3(1, 2, 3)
wobble = 0.5
)");
    REQUIRE(scene.has_value());

    const Entity entity = scene->firstRoot();
    CHECK(scene->get<Transform>(entity).position == Vec3{1.0f, 2.0f, 3.0f});
    REQUIRE(capture.warnings().size() == 2);
    CHECK(capture.warnings()[0] == "Scene line 3: skipping unknown component type 'Hologram'");
    CHECK(capture.warnings()[1] == "Scene line 7: skipping unknown field 'wobble' of Transform");
}

TEST_CASE("Malformed scenes report the faulty line", "[scene][serializer]")
{
    const auto check = [](std::string_view text, std::string_view message) {
        const auto scene = devex::scene::loadScene(text);
        REQUIRE_FALSE(scene.has_value());
        CHECK(scene.error().message == message);
    };

    check("[entity uuid=\"x\"]", "a scene file starts with [scene]");
    check("[scene format=2]", "line 1: unsupported scene format, expected format=1");
    check("[scene format=1]\n[entity name=\"No id\"]", "line 2: an entity needs a uuid");
    check("[scene format=1]\n[component type=\"Transform\"]",
          "line 2: a component must follow an entity");
    check(R"([scene format=1]
[entity uuid="6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23"]
[component type="Transform"]
position = vec3(1, 2))",
          "line 4: Transform.position: expected vec3() with 3 numbers");
    check(R"([scene format=1]
[entity uuid="6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23"]
parent = "b41e7c02-9d3a-4f6e-8c11-5a2e9b7d0f44")",
          "line 2: parent b41e7c02-9d3a-4f6e-8c11-5a2e9b7d0f44 does not exist");
}
