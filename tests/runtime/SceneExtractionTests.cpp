#include <devex/asset/AssetId.hpp>
#include <devex/runtime/SceneExtraction.hpp>
#include <devex/scene/Components.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using devex::math::Vec3;
using devex::scene::Entity;
using devex::scene::Scene;
using devex::scene::Transform;

TEST_CASE("Extraction copies the camera, the light and the loaded meshes", "[runtime][extraction]")
{
    Scene scene;
    devex::runtime::AssetRegistry assets;
    const devex::render::MeshHandle cubeHandle{4, 1};
    assets.registerMesh(devex::asset::builtin::cubeMesh, cubeHandle);

    const Entity camera = scene.createEntity("Camera");
    scene.add<Transform>(camera, Transform{.position = {0.0f, 2.0f, 5.0f}});
    scene.add<devex::scene::Camera>(camera, devex::scene::Camera{.verticalFov = 1.0f});

    const Entity sun = scene.createEntity("Sun");
    scene.add<Transform>(sun, Transform{.rotation = devex::math::angleAxis(
                                            devex::math::radians(-90.0f), Vec3{1.0f, 0.0f, 0.0f})});
    scene.add<devex::scene::DirectionalLight>(sun, devex::scene::DirectionalLight{.ambient = 0.4f});

    const Entity cube = scene.createEntity("Cube");
    scene.add<Transform>(cube, Transform{.position = {3.0f, 0.0f, 0.0f}});
    scene.add<devex::scene::MeshRenderer>(cube, devex::asset::builtin::cubeMesh);

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
    // The sun is pitched down by 90 degrees, so its -Z axis points to -Y.
    CHECK_THAT(world.lightDirection.y, WithinAbs(-1.0, 1e-5));
    CHECK(world.ambient == 0.4f);
    REQUIRE(world.meshes.size() == 1);
    CHECK(world.meshes[0].mesh == cubeHandle);
    CHECK(world.meshes[0].transform[3].x == 3.0f);
}
