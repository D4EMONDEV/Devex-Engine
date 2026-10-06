#include <devex/asset/AssetId.hpp>
#include <devex/runtime/SceneExtraction.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/Light2DComponents.hpp>
#include <devex/scene/TilemapComponents.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <numbers>
#include <utility>
#include <vector>

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

TEST_CASE("Extraction adds the particles of the emitters and the ribbons of the trails", "[runtime][extraction][particles]")
{
    Scene scene;
    devex::runtime::AssetManager assets(nullptr, nullptr);
    devex::particles::ParticleWorld particles;

    devex::scene::ParticleEmitter settings;
    settings.rate = 0.0f;
    settings.burst = 4;
    settings.looping = false;
    settings.lifetime = {10.0f, 10.0f};
    settings.speed = {1.0f, 1.0f};
    settings.shape = devex::scene::ParticleShape::Cone;
    settings.radius = 0.0f;
    settings.angle = 0.0f;
    settings.space = devex::scene::ParticleSpace::Local;
    settings.blend = devex::scene::ParticleBlend::Additive;
    settings.renderMode = devex::scene::ParticleRenderMode::Stretched;
    settings.trails = true;
    settings.trailTime = 1.0f;
    const Entity emitter = scene.createEntity("Sparks");
    scene.add<Transform>(emitter, Transform{.position = {2.0f, 0.0f, 0.0f}});
    scene.add<devex::scene::ParticleEmitter>(emitter, settings);

    const Entity ball = scene.createEntity("Ball");
    scene.add<Transform>(ball);
    scene.add<devex::scene::TrailRenderer>(ball, devex::scene::TrailRenderer{.minDistance = 0.1f, .width = 0.3f});

    for (int frame = 0; frame < 8; ++frame)
    {
        scene.get<Transform>(ball).position.y = static_cast<float>(frame) * 0.5f;
        scene.updateTransforms();
        particles.update(scene, devex::core::Duration(0.05));
    }

    devex::render::RenderWorld world;
    devex::runtime::extractParticles(particles, assets, world);
    REQUIRE(world.particles.size() == 4);
    for (const devex::render::RenderParticle& particle : world.particles)
    {
        // In local space, placed where the emitter stands, and streaked along their motion.
        CHECK_THAT(particle.position.x, WithinAbs(2.0, 1e-4));
        CHECK(particle.stretch.z < 0.0f);
    }
    std::size_t batches = 0;
    std::size_t ribbons = 0;
    for (const devex::render::ParticleDraw& draw : world.particleDraws)
    {
        if (draw.ribbons)
        {
            ++ribbons;
            CHECK(draw.count > 0);
            for (std::uint32_t segment = draw.first; segment < draw.first + draw.count; ++segment)
            {
                CHECK(world.trailSegments[segment] + 1 < world.trailPoints.size());
            }
        }
        else
        {
            ++batches;
            CHECK(draw.count == 4);
            CHECK(draw.blend == devex::render::ParticleBlend::Additive);
            CHECK(draw.facing == devex::render::ParticleFacing::Stretched);
            CHECK_THAT(draw.center.x, WithinAbs(2.0, 1e-4));
        }
    }
    CHECK(batches == 1);
    // The trails of the particles, and the ribbon of the ball.
    CHECK(ribbons == 2);
}

TEST_CASE("Extraction adds the 2D lights, the tint of the canvas and the occluders", "[runtime][extraction][light2d]")
{
    Scene scene;
    devex::runtime::AssetManager assets(nullptr, nullptr);
    const Entity lamp = scene.createEntity("Lamp");
    scene.add<Transform>(lamp, Transform{.position = {1.0f, 2.0f, 3.0f}});
    scene.add<devex::scene::PointLight2D>(lamp, devex::scene::PointLight2D{.color = {1.0f, 0.5f, 0.25f}, .energy = 2.0f, .radius = 3.0f});
    // Turned a quarter of a turn, the light that goes down goes to the right.
    const Entity moon = scene.createEntity("Moon");
    scene.add<Transform>(moon, Transform{.rotation = devex::math::angleAxis(devex::math::radians(90.0f), Vec3{0.0f, 0.0f, 1.0f})});
    scene.add<devex::scene::DirectionalLight2D>(moon, devex::scene::DirectionalLight2D{.height = 0.25f});
    const Entity box = scene.createEntity("Box");
    scene.add<Transform>(box, Transform{.position = {5.0f, 0.0f, 0.0f}});
    scene.add<devex::scene::LightOccluder2D>(box);
    const Entity night = scene.createEntity("Night");
    scene.add<devex::scene::CanvasModulate>(night, devex::scene::CanvasModulate{.color = {0.2f, 0.3f, 0.4f}});
    scene.add<devex::scene::CanvasModulate>(scene.createEntity("Second"), devex::scene::CanvasModulate{.color = {1.0f, 0.0f, 0.0f}});
    scene.updateTransforms();

    devex::render::RenderWorld world;
    devex::runtime::extractLights2D(scene, assets, world);
    REQUIRE(world.lights2D.size() == 2);
    const devex::render::RenderLight2D& point = world.lights2D[0];
    CHECK_FALSE(point.directional);
    CHECK(point.position == devex::math::Vec2{1.0f, 2.0f});
    CHECK(point.color == Vec3{2.0f, 1.0f, 0.5f});
    CHECK(point.radius == 3.0f);
    const devex::render::RenderLight2D& directional = world.lights2D[1];
    CHECK(directional.directional);
    CHECK_THAT(directional.direction.x, WithinAbs(1.0, 1e-5));
    CHECK_THAT(directional.direction.y, WithinAbs(0.0, 1e-5));
    CHECK(directional.height == 0.25f);
    CHECK(world.canvasModulate == Vec3{0.2f, 0.3f, 0.4f});
    // No light casts shadows: the occluders are left out.
    CHECK(world.occluders2D.empty());

    // Once one does, the square gives its four edges where its entity stands; open, it gives three.
    scene.get<devex::scene::PointLight2D>(lamp).shadows = true;
    world = {};
    devex::runtime::extractLights2D(scene, assets, world);
    REQUIRE(world.occluders2D.size() == 4);
    CHECK(world.occluders2D[0].from == devex::math::Vec2{4.5f, -0.5f});
    CHECK(world.occluders2D[0].to == devex::math::Vec2{5.5f, -0.5f});
    CHECK(world.occluders2D[3].to == devex::math::Vec2{4.5f, -0.5f});
    scene.get<devex::scene::LightOccluder2D>(box).closed = false;
    scene.get<devex::scene::LightOccluder2D>(box).mask = 6;
    world = {};
    devex::runtime::extractLights2D(scene, assets, world);
    REQUIRE(world.occluders2D.size() == 3);
    CHECK(world.occluders2D[0].mask == 6);
}

TEST_CASE("The tiles that occlude make one outline, joined along rows and columns", "[runtime][extraction][light2d]")
{
    devex::asset::TilesetData tileset;
    tileset.tiles = {{.id = 1, .occluder = true}, {.id = 2}};
    // An L of occluding tiles, with a tile that does not in its corner.
    devex::scene::TileGrid grid;
    for (const devex::math::IVec2 cell : {devex::math::IVec2{0, 0}, devex::math::IVec2{1, 0}, devex::math::IVec2{2, 0}, devex::math::IVec2{0, 1}})
    {
        grid.set(cell, 1);
    }
    grid.set({1, 1}, 2);
    std::vector<std::pair<devex::math::IVec2, devex::math::IVec2>> outline = devex::runtime::tileOccluderOutline(grid, tileset);
    const auto key = [](const std::pair<devex::math::IVec2, devex::math::IVec2>& edge) {
        return std::array{edge.first.x, edge.first.y, edge.second.x, edge.second.y};
    };
    std::ranges::sort(outline, {}, key);
    const std::vector<std::pair<devex::math::IVec2, devex::math::IVec2>> expected{
        {{0, 0}, {0, 2}}, {{0, 0}, {3, 0}}, {{0, 2}, {1, 2}}, {{1, 1}, {1, 2}}, {{1, 1}, {3, 1}}, {{3, 0}, {3, 1}},
    };
    CHECK(outline == expected);
}
