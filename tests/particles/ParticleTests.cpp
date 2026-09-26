#include <devex/core/JobSystem.hpp>
#include <devex/particles/ParticleWorld.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/ParticleComponents.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <vector>

using Catch::Approx;
using devex::core::Duration;
using devex::math::Vec3;
using devex::particles::Particle;
using devex::particles::ParticleWorld;
using devex::scene::Entity;
using devex::scene::ParticleEmitter;
using devex::scene::Scene;
using devex::scene::Transform;

namespace {

// An emitter whose particles live long, all from the same point, straight up at 1 m/s.
[[nodiscard]] ParticleEmitter steady()
{
    ParticleEmitter settings;
    settings.rate = 10.0f;
    settings.shape = devex::scene::ParticleShape::Cone;
    settings.radius = 0.0f;
    settings.angle = 0.0f;
    settings.lifetime = {100.0f, 100.0f};
    settings.speed = {1.0f, 1.0f};
    settings.size = {1.0f, 1.0f};
    settings.seed = 7;
    return settings;
}

Entity emitter(Scene& scene, const ParticleEmitter& settings, Vec3 position = Vec3{0.0f})
{
    const Entity entity = scene.createEntity("Emitter");
    scene.add<Transform>(entity).position = position;
    scene.add<ParticleEmitter>(entity, settings);
    scene.updateTransforms();
    return entity;
}

void step(ParticleWorld& world, Scene& scene, double seconds, int frames = 1)
{
    for (int frame = 0; frame < frames; ++frame)
    {
        scene.updateTransforms();
        world.update(scene, Duration(seconds));
    }
}

[[nodiscard]] std::vector<Particle> particlesOf(const ParticleWorld& world)
{
    std::vector<Particle> particles;
    world.forEachEmitter([&](const devex::particles::EmitterView& view) {
        particles.insert(particles.end(), view.particles.begin(), view.particles.end());
    });
    return particles;
}

} // namespace

TEST_CASE("An emitter emits at its rate, in bursts, and up to its maximum", "[particles]")
{
    Scene scene;
    ParticleWorld world;
    ParticleEmitter settings = steady();
    const Entity entity = emitter(scene, settings);
    step(world, scene, 0.1, 10);
    CHECK(world.particleCount(entity) == 10);
    CHECK(world.isPlaying(entity));

    // A burst at the start of every cycle.
    Scene bursts;
    ParticleWorld burstWorld;
    settings.rate = 0.0f;
    settings.burst = 5;
    settings.duration = 1.0f;
    const Entity burster = emitter(bursts, settings);
    step(burstWorld, bursts, 0.0);
    CHECK(burstWorld.particleCount(burster) == 5);
    step(burstWorld, bursts, 0.25, 4);
    CHECK(burstWorld.particleCount(burster) == 10);

    // No more than the maximum.
    Scene crowded;
    ParticleWorld crowdedWorld;
    settings = steady();
    settings.rate = 1000.0f;
    settings.maxParticles = 50;
    const Entity capped = emitter(crowded, settings);
    step(crowdedWorld, crowded, 0.1, 5);
    CHECK(crowdedWorld.particleCount(capped) == 50);
}

TEST_CASE("Particles die at the end of their life, and a cycle that does not loop stops", "[particles]")
{
    Scene scene;
    ParticleWorld world;
    ParticleEmitter settings = steady();
    settings.lifetime = {0.5f, 0.5f};
    settings.looping = false;
    settings.duration = 1.0f;
    const Entity entity = emitter(scene, settings);
    step(world, scene, 0.1, 5);
    const std::size_t alive = world.particleCount(entity);
    CHECK(alive >= 4);
    CHECK(alive <= 5);
    step(world, scene, 0.1, 10);
    // The cycle is over, and so are the last particles.
    CHECK(world.particleCount(entity) == 0);
    CHECK_FALSE(world.isPlaying(entity));

    // Played again by code.
    world.play(scene, entity);
    step(world, scene, 0.1, 3);
    CHECK(world.particleCount(entity) == 3);
    // Stopped, the particles alive finish; cleared, they vanish.
    world.stop(entity);
    step(world, scene, 0.1);
    CHECK(world.particleCount(entity) == 3);
    world.stop(entity, true);
    CHECK(world.particleCount(entity) == 0);
    world.emit(scene, entity, 7);
    step(world, scene, 0.0);
    CHECK(world.particleCount(entity) == 7);
}

TEST_CASE("A prewarmed emitter starts full, and one that does not play on start waits for code", "[particles]")
{
    Scene scene;
    ParticleWorld world;
    ParticleEmitter settings = steady();
    settings.prewarm = true;
    settings.duration = 2.0f;
    const Entity warm = emitter(scene, settings);
    settings.prewarm = false;
    settings.playOnStart = false;
    const Entity waiting = emitter(scene, settings);
    step(world, scene, 0.0);
    // Two seconds at ten per second.
    CHECK(world.particleCount(warm) >= 19);
    CHECK(world.particleCount(warm) <= 21);
    CHECK(world.particleCount(waiting) == 0);
    CHECK_FALSE(world.isPlaying(waiting));
}

TEST_CASE("Particles move with their speed, gravity, drag and curves", "[particles]")
{
    Scene scene;
    const devex::asset::AssetId half{devex::core::Uuid::generate()};
    auto curve = std::make_shared<devex::asset::CurveData>(devex::asset::CurveData{
        .keys = {{.time = 0.0f, .value = 0.5f}, {.time = 1.0f, .value = 0.5f}}});
    ParticleWorld world([&](devex::asset::AssetId id) -> std::shared_ptr<const devex::asset::CurveData> {
        return id == half ? curve : nullptr;
    });
    ParticleEmitter settings = steady();
    settings.rate = 0.0f;
    settings.burst = 1;
    settings.looping = false;
    settings.color = {1.0f, 0.0f, 0.0f, 1.0f};
    settings.endColor = {0.0f, 0.0f, 1.0f, 1.0f};
    settings.lifetime = {2.0f, 2.0f};
    settings.sizeCurve = half;
    const Entity riser = emitter(scene, settings);
    step(world, scene, 0.0);
    step(world, scene, 0.1, 10);
    std::vector<Particle> particles = particlesOf(world);
    REQUIRE(particles.size() == 1);
    // Straight ahead is -Z, the axis of the cone.
    CHECK(particles[0].position.z == Approx(-1.0f).margin(1e-3));
    CHECK(particles[0].size == Approx(0.5f));
    // Halfway through its life, halfway from red to blue.
    CHECK(particles[0].color.r == Approx(0.5f).margin(1e-3));
    CHECK(particles[0].color.b == Approx(0.5f).margin(1e-3));
    scene.destroyEntity(riser);

    // Gravity pulls down, drag slows down.
    settings = steady();
    settings.rate = 0.0f;
    settings.burst = 1;
    settings.looping = false;
    settings.speed = {0.0f, 0.0f};
    settings.gravity = 1.0f;
    emitter(scene, settings);
    settings.gravity = 0.0f;
    settings.speed = {10.0f, 10.0f};
    settings.drag = 2.0f;
    emitter(scene, settings, Vec3{5.0f, 0.0f, 0.0f});
    step(world, scene, 0.0);
    step(world, scene, 0.1, 10);
    particles = particlesOf(world);
    REQUIRE(particles.size() == 2);
    const Particle& falling = particles[0].position.x < 2.5f ? particles[0] : particles[1];
    const Particle& slowed = particles[0].position.x < 2.5f ? particles[1] : particles[0];
    CHECK(falling.position.y < -4.0f);
    CHECK(falling.velocity.y == Approx(-9.81f).margin(0.01));
    CHECK(devex::math::length(slowed.velocity) < 2.0f);
}

TEST_CASE("Particles stay in the world or follow their emitter", "[particles]")
{
    Scene scene;
    ParticleWorld world;
    ParticleEmitter settings = steady();
    settings.rate = 0.0f;
    settings.burst = 1;
    settings.looping = false;
    settings.speed = {0.0f, 0.0f};
    const Entity inWorld = emitter(scene, settings);
    settings.space = devex::scene::ParticleSpace::Local;
    const Entity inLocal = emitter(scene, settings);
    step(world, scene, 0.0);
    scene.get<Transform>(inWorld).position = Vec3{3.0f, 0.0f, 0.0f};
    scene.get<Transform>(inLocal).position = Vec3{3.0f, 0.0f, 0.0f};
    step(world, scene, 0.1);

    world.forEachEmitter([&](const devex::particles::EmitterView& view) {
        REQUIRE(view.particles.size() == 1);
        const Vec3 local = view.particles[0].position;
        const Vec3 placed = view.settings->space == devex::scene::ParticleSpace::Local
                                ? Vec3(view.transform * devex::math::Vec4(local, 1.0f))
                                : local;
        CHECK(placed.x == Approx(view.entity == inWorld ? 0.0f : 3.0f).margin(1e-4));
    });

    // The rate over distance emits along the way.
    Scene moving;
    ParticleWorld movingWorld;
    settings = steady();
    settings.rate = 0.0f;
    settings.rateOverDistance = 2.0f;
    const Entity walker = emitter(moving, settings);
    step(movingWorld, moving, 0.1);
    for (int frame = 1; frame <= 10; ++frame)
    {
        moving.get<Transform>(walker).position.x = static_cast<float>(frame) * 0.5f;
        step(movingWorld, moving, 0.1);
    }
    CHECK(movingWorld.particleCount(walker) == 10);
}

TEST_CASE("Particles bounce off what the collision query finds, and emit where they hit or die", "[particles]")
{
    Scene scene;
    ParticleWorld world;
    // The ground, at y = 0.
    world.setCollisionQuery([](Vec3 from, Vec3 to) -> std::optional<ParticleWorld::Hit> {
        if (from.y >= 0.0f && to.y < 0.0f)
        {
            const float t = from.y / (from.y - to.y);
            return ParticleWorld::Hit{.point = from + (to - from) * t, .normal = Vec3{0.0f, 1.0f, 0.0f}};
        }
        return std::nullopt;
    });
    ParticleEmitter sparks = steady();
    sparks.rate = 0.0f;
    sparks.lifetime = {5.0f, 5.0f};
    sparks.speed = {0.0f, 0.0f};
    sparks.playOnStart = false;
    const Entity sparkEmitter = emitter(scene, sparks);

    ParticleEmitter settings = steady();
    settings.rate = 0.0f;
    settings.burst = 1;
    settings.looping = false;
    settings.speed = {0.0f, 0.0f};
    settings.gravity = 1.0f;
    settings.collide = true;
    settings.bounce = 0.5f;
    settings.subEmitter = devex::scene::EntityRef{scene.uuid(sparkEmitter)};
    settings.subEmitOn = devex::scene::SubEmitTrigger::Collision;
    settings.subEmitCount = 3;
    const Entity ball = emitter(scene, settings, Vec3{0.0f, 1.0f, 0.0f});
    step(world, scene, 0.0);
    bool bounced = false;
    for (int frame = 0; frame < 20 && !bounced; ++frame)
    {
        step(world, scene, 0.05);
        world.forEachEmitter([&](const devex::particles::EmitterView& view) {
            if (view.entity == ball && !view.particles.empty() && view.particles[0].velocity.y > 0.0f)
            {
                bounced = true;
                CHECK(view.particles[0].position.y >= 0.0f);
            }
        });
    }
    CHECK(bounced);
    // The hit emitted sparks from another emitter, which does not emit by itself.
    CHECK(world.particleCount(sparkEmitter) == 3);

    // A hit that takes the whole life ends the particle there, and its death emits too.
    world.stop(ball, true);
    settings.lifetimeLoss = 1.0f;
    settings.subEmitOn = devex::scene::SubEmitTrigger::Death;
    const Entity dropped = emitter(scene, settings, Vec3{2.0f, 0.5f, 0.0f});
    step(world, scene, 0.0);
    step(world, scene, 0.05, 30);
    CHECK(world.particleCount(dropped) == 0);
    CHECK(world.particleCount(sparkEmitter) == 6);
}

TEST_CASE("Particles and entities leave trails that fade with time", "[particles]")
{
    Scene scene;
    ParticleWorld world;
    ParticleEmitter settings = steady();
    settings.rate = 0.0f;
    settings.burst = 3;
    settings.looping = false;
    settings.speed = {5.0f, 5.0f};
    settings.trails = true;
    settings.trailTime = 0.4f;
    emitter(scene, settings);
    step(world, scene, 0.0);
    step(world, scene, 0.05, 6);
    std::size_t withPoints = 0;
    world.forEachEmitter([&](const devex::particles::EmitterView& view) {
        for (const Particle& particle : view.particles)
        {
            withPoints += view.trailCounts[particle.trail] > 1 ? 1 : 0;
        }
    });
    CHECK(withPoints == 3);

    const Entity projectile = scene.createEntity("Projectile");
    scene.add<Transform>(projectile);
    scene.add<devex::scene::TrailRenderer>(projectile, devex::scene::TrailRenderer{.time = 0.3f, .minDistance = 0.1f});
    for (int frame = 0; frame < 10; ++frame)
    {
        scene.get<Transform>(projectile).position.x = static_cast<float>(frame) * 0.5f;
        step(world, scene, 0.05);
    }
    std::size_t points = 0;
    world.forEachTrail([&](const devex::particles::TrailView& trail) {
        points = trail.points.size();
        CHECK(trail.head.x == Approx(4.5f));
    });
    // Points older than 0.3 seconds are gone.
    CHECK(points >= 5);
    CHECK(points <= 7);
    scene.get<devex::scene::TrailRenderer>(projectile).emitting = false;
    step(world, scene, 0.1, 5);
    points = 0;
    world.forEachTrail([&](const devex::particles::TrailView& trail) { points = trail.points.size(); });
    CHECK(points == 0);
}

TEST_CASE("A seed plays the same particles, on the workers as on the calling thread", "[particles]")
{
    ParticleEmitter settings = steady();
    settings.shape = devex::scene::ParticleShape::Sphere;
    settings.radius = 1.0f;
    settings.speed = {0.5f, 3.0f};
    settings.noise = 2.0f;
    settings.seed = 42;
    devex::core::JobSystem jobs(4);
    std::vector<Vec3> positions[2];
    for (int run = 0; run < 2; ++run)
    {
        Scene scene;
        ParticleWorld world({}, run == 1 ? &jobs : nullptr);
        Entity first;
        for (int index = 0; index < 8; ++index)
        {
            const Entity created = emitter(scene, settings, Vec3{static_cast<float>(index), 0.0f, 0.0f});
            first = index == 0 ? created : first;
        }
        step(world, scene, 0.05, 20);
        CHECK(world.particleCount() == 8 * 10);
        CHECK(world.emitterCount() == 8);
        world.forEachEmitter([&](const devex::particles::EmitterView& view) {
            if (view.entity == first)
            {
                for (const Particle& particle : view.particles)
                {
                    positions[run].push_back(particle.position);
                }
            }
        });
    }
    REQUIRE(positions[0].size() == positions[1].size());
    for (std::size_t index = 0; index < positions[0].size(); ++index)
    {
        CHECK(positions[0][index] == positions[1][index]);
    }
}

TEST_CASE("Emitters are saved in scenes, their settings grouped for the inspector", "[particles]")
{
    Scene scene;
    ParticleEmitter settings = steady();
    settings.shape = devex::scene::ParticleShape::Box;
    settings.blend = devex::scene::ParticleBlend::Additive;
    settings.renderMode = devex::scene::ParticleRenderMode::Stretched;
    settings.color = {1.0f, 0.5f, 0.25f, 0.75f};
    const Entity entity = emitter(scene, settings);
    scene.add<devex::scene::TrailRenderer>(entity, devex::scene::TrailRenderer{.width = 0.5f});
    const std::string text = devex::scene::saveScene(scene);
    CHECK(text.find("shape = \"box\"") != std::string::npos);
    CHECK(text.find("render_mode = \"stretched\"") != std::string::npos);
    devex::core::Result<Scene> loaded = devex::scene::loadScene(text);
    REQUIRE(loaded.has_value());
    const Entity copy = loaded->findEntity(scene.uuid(entity));
    REQUIRE(loaded->has<ParticleEmitter>(copy));
    CHECK(loaded->get<ParticleEmitter>(copy).blend == devex::scene::ParticleBlend::Additive);
    CHECK(loaded->get<ParticleEmitter>(copy).color == settings.color);
    CHECK(loaded->get<devex::scene::TrailRenderer>(copy).width == 0.5f);

    const devex::scene::ComponentType* const type = devex::scene::componentRegistry().find("ParticleEmitter");
    REQUIRE(type != nullptr);
    CHECK(type->type->fields.front().group == "Emitter");
    CHECK(type->type->findField("rate")->group == "Emission");
    CHECK(type->type->findField("rate_over_distance")->group.empty());
    CHECK(type->type->findField("trails")->group == "Trails");
}
