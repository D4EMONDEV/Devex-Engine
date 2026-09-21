#include <devex/asset/AssetId.hpp>
#include <devex/core/Log.hpp>
#include <devex/scene/AnimationComponents.hpp>
#include <devex/scene/AudioComponents.hpp>
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
material = asset("00000000-0000-0000-0000-000000000000")
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

TEST_CASE("Unknown components are preserved and unknown fields skipped with warnings", "[scene][serializer]")
{
    const WarningCapture capture;
    auto scene = devex::scene::loadScene(R"([scene format=1]
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
    REQUIRE(capture.warnings().size() == 1);
    CHECK(capture.warnings()[0] == "Scene line 7: skipping unknown field 'wobble' of Transform");

    // The unknown component is written back as it was read.
    const std::string saved = devex::scene::saveScene(*scene);
    CHECK(saved.find("[component type=\"Hologram\"]\nbrightness = 3\n") != std::string::npos);
    CHECK(devex::scene::restorePreservedComponents(*scene) == 0);
}

namespace test {

struct Hologram
{
    float brightness = 1.0f;
};
DEVEX_DECLARE_REFLECTION(Hologram);
DEVEX_REFLECT(Hologram)
{
    type.field("brightness", &Hologram::brightness);
}

} // namespace test

TEST_CASE("Components are preserved while their type is unregistered, then restored", "[scene][serializer]")
{
    Scene scene;
    const Entity entity = scene.createEntity("Hologram");
    scene.add<Transform>(entity);
    devex::scene::registerComponent<test::Hologram>();
    scene.add<test::Hologram>(entity).brightness = 7.0f;

    // Unloading the module that defines the type preserves its components and destroys the pool.
    const std::size_t index = devex::scene::componentTypeIndex<test::Hologram>();
    CHECK(devex::scene::preserveComponentPool(scene, index) == 1);
    CHECK(scene.componentPool(index) == nullptr);
    CHECK(devex::scene::componentRegistry().remove("Hologram"));
    CHECK_FALSE(devex::scene::componentRegistry().remove("Hologram"));
    CHECK(scene.has<devex::scene::PreservedComponents>(entity));

    // Copies and saved files keep the preserved component.
    Scene copy = scene.clone();
    auto reloaded = devex::scene::loadScene(devex::scene::saveScene(copy));
    REQUIRE(reloaded.has_value());

    // Once the type is registered again, the components come back.
    devex::scene::registerComponent<test::Hologram>();
    CHECK(devex::scene::restorePreservedComponents(scene) == 1);
    CHECK(scene.get<test::Hologram>(entity).brightness == 7.0f);
    CHECK_FALSE(scene.has<devex::scene::PreservedComponents>(entity));
    CHECK(scene.has<Transform>(entity));
    CHECK(devex::scene::restorePreservedComponents(*reloaded) == 1);
    CHECK(reloaded->get<test::Hologram>(reloaded->firstRoot()).brightness == 7.0f);
    CHECK(devex::scene::componentRegistry().remove("Hologram"));
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

TEST_CASE("Enumerations are saved by name and unknown names are errors", "[scene][serializer]")
{
    Scene scene;
    const Entity camera = scene.createEntity("Camera");
    scene.add<devex::scene::Camera>(camera, devex::scene::Camera{.tonemapper = devex::scene::Tonemapper::PbrNeutral});

    const std::string text = devex::scene::saveScene(scene);
    CHECK(text.find("tonemapper = \"pbr_neutral\"") != std::string::npos);

    const auto loaded = devex::scene::loadScene(text);
    REQUIRE(loaded.has_value());
    const Entity loadedCamera = loaded->findEntity(scene.uuid(camera));
    CHECK(loaded->get<devex::scene::Camera>(loadedCamera).tonemapper == devex::scene::Tonemapper::PbrNeutral);

    std::string broken = text;
    broken.replace(broken.find("pbr_neutral"), 11, "sepia");
    const auto failed = devex::scene::loadScene(broken);
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().message.find("agx") != std::string::npos);
}

TEST_CASE("Sound sources and listeners are saved with their settings", "[scene][serializer][audio]")
{
    Scene scene;
    const devex::asset::AssetId clip = devex::asset::AssetId::generate();
    const Entity speaker = scene.createEntity("Speaker");
    scene.add<devex::scene::AudioSource>(speaker, devex::scene::AudioSource{
                                                      .clip = clip,
                                                      .volume = 0.5f,
                                                      .pitch = 1.25f,
                                                      .loop = true,
                                                      .playOnStart = false,
                                                      .spatial = true,
                                                      .minDistance = 2.0f,
                                                      .maxDistance = 12.0f,
                                                      .attenuation = devex::scene::AudioAttenuation::Linear,
                                                      .rolloff = 0.5f,
                                                      .doppler = 0.0f,
                                                      .group = 2,
                                                  });
    const Entity listener = scene.createEntity("Ears");
    scene.add<devex::scene::AudioListener>(listener);

    const std::string text = devex::scene::saveScene(scene);
    CHECK(text.find("attenuation = \"linear\"") != std::string::npos);
    const auto loaded = devex::scene::loadScene(text);
    REQUIRE(loaded.has_value());
    const devex::scene::AudioSource& source = loaded->get<devex::scene::AudioSource>(loaded->findEntity(scene.uuid(speaker)));
    CHECK(source.clip == clip);
    CHECK(source.volume == 0.5f);
    CHECK(source.pitch == 1.25f);
    CHECK(source.loop);
    CHECK_FALSE(source.playOnStart);
    CHECK(source.minDistance == 2.0f);
    CHECK(source.maxDistance == 12.0f);
    CHECK(source.attenuation == devex::scene::AudioAttenuation::Linear);
    CHECK(source.rolloff == 0.5f);
    CHECK(source.doppler == 0.0f);
    CHECK(source.group == 2);
    CHECK(loaded->has<devex::scene::AudioListener>(loaded->findEntity(scene.uuid(listener))));

    // The inspector knows which fields are clips and groups.
    const devex::scene::ComponentType* const type = devex::scene::componentRegistry().find("AudioSource");
    REQUIRE(type != nullptr);
    CHECK(type->type->findField("clip")->assetType == "audio");
    CHECK(type->type->findField("group")->audioGroup);
}

TEST_CASE("Skinned meshes and animators are saved with their bones", "[scene][serializer][animation]")
{
    Scene scene;
    const devex::asset::AssetId mesh = devex::asset::AssetId::generate();
    const devex::asset::AssetId clip = devex::asset::AssetId::generate();
    const Entity character = scene.createEntity("Robot");
    scene.add<devex::scene::Animator>(character, devex::scene::Animator{
                                                     .clip = clip,
                                                     .speed = 1.5f,
                                                     .loop = false,
                                                     .playOnStart = false,
                                                     .blendTime = 0.4f,
                                                     .applyRootMotion = true,
                                                 });
    const Entity hips = scene.createEntity("Hips");
    REQUIRE(scene.setParent(hips, character).has_value());
    const Entity spine = scene.createEntity("Spine");
    REQUIRE(scene.setParent(spine, hips).has_value());
    scene.add<devex::scene::SkinnedMeshRenderer>(
        character, devex::scene::SkinnedMeshRenderer{
                       .mesh = mesh,
                       .bones = {devex::scene::EntityRef{scene.uuid(hips)},
                                 devex::scene::EntityRef{scene.uuid(spine)}},
                   });

    const std::string text = devex::scene::saveScene(scene);
    CHECK(text.find("bones = list(entity(") != std::string::npos);
    const auto loaded = devex::scene::loadScene(text);
    REQUIRE(loaded.has_value());
    const Entity reloaded = loaded->findEntity(scene.uuid(character));
    const devex::scene::Animator& animator = loaded->get<devex::scene::Animator>(reloaded);
    CHECK(animator.clip == clip);
    CHECK(animator.speed == 1.5f);
    CHECK_FALSE(animator.loop);
    CHECK_FALSE(animator.playOnStart);
    CHECK(animator.blendTime == 0.4f);
    CHECK(animator.applyRootMotion);

    const devex::scene::SkinnedMeshRenderer& renderer =
        loaded->get<devex::scene::SkinnedMeshRenderer>(reloaded);
    CHECK(renderer.mesh == mesh);
    REQUIRE(renderer.bones.size() == 2);
    CHECK(loaded->name(loaded->resolve(renderer.bones[0])) == "Hips");
    CHECK(loaded->name(loaded->resolve(renderer.bones[1])) == "Spine");

    // The inspector knows which field holds an animation.
    const devex::scene::ComponentType* const type = devex::scene::componentRegistry().find("Animator");
    REQUIRE(type != nullptr);
    CHECK(type->type->findField("clip")->assetType == "animation");
}
