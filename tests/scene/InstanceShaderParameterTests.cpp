#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/InstanceShaderParameters.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/scene/SpriteComponents.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using devex::math::Vec4;
using devex::scene::Entity;
using devex::scene::MeshRenderer;
using devex::scene::Scene;
using devex::scene::SpriteRenderer;

TEST_CASE("Renderers keep the values objects give the instance uniforms of their shaders", "[scene][shader]")
{
    Scene scene;
    const Entity crystal = scene.createEntity("Crystal");
    scene.add<MeshRenderer>(crystal, MeshRenderer{.mesh = devex::asset::builtin::cubeMesh});
    const Entity knight = scene.createEntity("Knight");
    scene.add<SpriteRenderer>(knight);
    const Entity empty = scene.createEntity("Empty");
    scene.add<devex::scene::Transform>(empty);

    CHECK_FALSE(devex::scene::instanceShaderParameter(scene, crystal, "glow"));
    CHECK(devex::scene::setInstanceShaderParameter(scene, crystal, "glow", {1.0f, 0.5f, 0.0f, 1.0f}));
    CHECK(devex::scene::setInstanceShaderParameter(scene, crystal, "flash", {0.25f, 0.0f, 0.0f, 0.0f}));
    // A second value replaces the first.
    CHECK(devex::scene::setInstanceShaderParameter(scene, crystal, "glow", {0.0f, 1.0f, 0.0f, 1.0f}));
    CHECK(scene.get<MeshRenderer>(crystal).instanceShaderParameters == std::vector<std::string>{"glow", "flash"});
    CHECK(devex::scene::instanceShaderParameter(scene, crystal, "glow") == Vec4{0.0f, 1.0f, 0.0f, 1.0f});
    // An entity without a renderer keeps no values.
    CHECK_FALSE(devex::scene::setInstanceShaderParameter(scene, empty, "glow", Vec4{1.0f}));
    CHECK(devex::scene::resetInstanceShaderParameter(scene, crystal, "glow"));
    CHECK_FALSE(devex::scene::resetInstanceShaderParameter(scene, crystal, "glow"));
    CHECK(scene.get<MeshRenderer>(crystal).instanceShaderValues == std::vector<Vec4>{Vec4{0.25f, 0.0f, 0.0f, 0.0f}});

    // Generic code reaches them by reflection, on the types that keep them.
    const devex::scene::ComponentType* const sprite = devex::scene::componentRegistry().find("SpriteRenderer");
    REQUIRE(sprite != nullptr);
    const devex::scene::InstanceShaderValues values = devex::scene::instanceShaderValues(*sprite, sprite->findMutable(scene, knight));
    REQUIRE(values.isValid());
    values.set("flash", {1.0f, 0.0f, 0.0f, 0.0f});
    CHECK(devex::scene::instanceShaderParameter(scene, knight, "flash") == Vec4{1.0f, 0.0f, 0.0f, 0.0f});
    const devex::scene::ComponentType* const transform = devex::scene::componentRegistry().find("Transform");
    REQUIRE(transform != nullptr);
    CHECK_FALSE(devex::scene::instanceShaderValues(*transform, transform->findMutable(scene, empty)).isValid());

    // They are saved with the scene.
    const std::string text = devex::scene::saveScene(scene);
    INFO(text);
    CHECK(text.find("instance_shader_parameters") != std::string::npos);
    auto loaded = devex::scene::loadScene(text);
    REQUIRE(loaded.has_value());
    CHECK(devex::scene::instanceShaderParameter(*loaded, loaded->findEntity(scene.uuid(knight)), "flash") ==
          Vec4{1.0f, 0.0f, 0.0f, 0.0f});
    CHECK(devex::scene::saveScene(*loaded) == text);
}
