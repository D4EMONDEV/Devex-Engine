#include <devex/asset/AssetId.hpp>
#include <devex/runtime/SceneExtraction.hpp>
#include <devex/scene/Components.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <numbers>

using Catch::Matchers::WithinAbs;
using devex::math::Vec3;
using devex::scene::Entity;
using devex::scene::Scene;
using devex::scene::Transform;

TEST_CASE("Extraction copies the camera, the lights and the loaded meshes", "[runtime][extraction]")
{
    Scene scene;
    devex::runtime::AssetManager assets(nullptr, nullptr);
    const devex::render::MeshHandle cubeHandle{4, 1};
    assets.registerMesh(devex::asset::builtin::cubeMesh, cubeHandle);
    // Two submeshes, drawn as two instances.
    const devex::render::MeshHandle pairHandle{5, 1};
    const devex::asset::AssetId pairMesh = devex::asset::AssetId::generate();
    assets.registerMesh(pairMesh, pairHandle, {devex::asset::AssetId{}, devex::asset::AssetId{}});

    const Entity camera = scene.createEntity("Camera");
    scene.add<Transform>(camera, Transform{.position = {0.0f, 2.0f, 5.0f}});
    scene.add<devex::scene::Camera>(camera, devex::scene::Camera{
                                                .verticalFov = 1.0f,
                                                .autoExposure = false,
                                                .ev100 = 9.0f,
                                                .tonemapper = devex::scene::Tonemapper::Aces,
                                            });

    const Entity sun = scene.createEntity("Sun");
    scene.add<Transform>(sun, Transform{.rotation = devex::math::angleAxis(
                                            devex::math::radians(-90.0f), Vec3{1.0f, 0.0f, 0.0f})});
    scene.add<devex::scene::DirectionalLight>(
        sun, devex::scene::DirectionalLight{.color = {1.0f, 0.5f, 1.0f}, .illuminance = 1000.0f, .castShadows = false});

    // 4 pi lumens emit one candela in every direction.
    const Entity lamp = scene.createEntity("Lamp");
    scene.add<Transform>(lamp, Transform{.position = {1.0f, 2.0f, 3.0f}});
    scene.add<devex::scene::PointLight>(
        lamp, devex::scene::PointLight{.intensity = 4.0f * std::numbers::pi_v<float>, .range = 6.0f});
    const Entity spot = scene.createEntity("Spot");
    scene.add<Transform>(spot);
    scene.add<devex::scene::SpotLight>(spot, devex::scene::SpotLight{.innerAngle = 0.1f, .outerAngle = 0.2f});

    // The sky texture is not loaded: the environment keeps a uniform sky.
    const Entity sky = scene.createEntity("Sky");
    scene.add<devex::scene::Environment>(sky, devex::scene::Environment{
                                                  .sky = devex::asset::AssetId::generate(),
                                                  .intensity = 500.0f,
                                                  .rotation = 1.0f,
                                              });

    const Entity cube = scene.createEntity("Cube");
    scene.add<Transform>(cube, Transform{.position = {3.0f, 0.0f, 0.0f}});
    scene.add<devex::scene::MeshRenderer>(cube, devex::asset::builtin::cubeMesh);

    const Entity pair = scene.createEntity("Pair");
    scene.add<Transform>(pair);
    scene.add<devex::scene::MeshRenderer>(pair, pairMesh);

    // A mesh that is not loaded is not drawn.
    const Entity missing = scene.createEntity("Missing");
    scene.add<Transform>(missing);
    scene.add<devex::scene::MeshRenderer>(missing, devex::asset::AssetId::generate());

    scene.updateTransforms();
    devex::render::RenderWorld world;
    devex::runtime::extractScene(scene, assets, world);

    CHECK(world.camera.verticalFov == 1.0f);
    CHECK_THAT(world.camera.view[3].y, WithinAbs(-2.0, 1e-5));
    CHECK_THAT(world.camera.view[3].z, WithinAbs(-5.0, 1e-5));
    CHECK_FALSE(world.camera.autoExposure);
    CHECK(world.camera.ev100 == 9.0f);
    CHECK(world.camera.tonemapper == devex::render::Tonemapper::Aces);

    // The sun is pitched down by 90 degrees, so its -Z axis points to -Y. The default
    // temperature of 6500 K is white.
    CHECK_THAT(world.sun.direction.y, WithinAbs(-1.0, 1e-5));
    CHECK_THAT(world.sun.illuminance.g, WithinAbs(500.0, 0.5));
    CHECK_FALSE(world.sun.castShadows);

    REQUIRE(world.lights.size() == 2);
    CHECK(world.lights[0].type == devex::render::LightType::Point);
    CHECK(world.lights[0].position == Vec3{1.0f, 2.0f, 3.0f});
    CHECK_THAT(world.lights[0].intensity.r, WithinAbs(1.0, 1e-3));
    CHECK(world.lights[0].range == 6.0f);
    CHECK(world.lights[1].type == devex::render::LightType::Spot);
    CHECK_THAT(world.lights[1].direction.z, WithinAbs(-1.0, 1e-5));
    CHECK(world.lights[1].outerAngle == 0.2f);

    CHECK_FALSE(world.environment.sky.isValid());
    CHECK(world.environment.intensity == 500.0f);
    CHECK(world.environment.rotation == 1.0f);

    REQUIRE(world.meshes.size() == 3);
    CHECK(world.meshes[0].mesh == cubeHandle);
    CHECK(world.meshes[0].submesh == 0);
    CHECK_FALSE(world.meshes[0].material.isValid());
    CHECK(world.meshes[0].transform[3].x == 3.0f);
    CHECK(world.meshes[1].mesh == pairHandle);
    CHECK(world.meshes[2].submesh == 1);
}
