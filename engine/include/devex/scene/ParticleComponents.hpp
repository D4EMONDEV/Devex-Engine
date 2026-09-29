#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/AssetId.hpp>
#include <devex/math/Math.hpp>
#include <devex/reflection/Reflection.hpp>
#include <devex/scene/EntityRef.hpp>

#include <array>
#include <cstdint>
#include <string_view>

namespace devex::scene {

// Where particles live once emitted.
enum class ParticleSpace : std::uint8_t
{
    // They stay where they were emitted when the emitter moves: smoke, sparks, dust.
    World,
    // They move with the emitter: the flame of a torch that is carried.
    Local,
};

// Where particles start around the emitter, and the way they go. The shape faces the -Z of the
// entity, as a camera does.
enum class ParticleShape : std::uint8_t
{
    // From the center, in every direction.
    Point,
    Sphere,
    // The half of a sphere in front of the emitter.
    Hemisphere,
    // From a disc of the radius, spreading by the angle: a fountain, a jet, a flame.
    Cone,
    // From inside a box, straight ahead: rain, snow.
    Box,
    // From a circle around the emitter, outwards: a shockwave.
    Circle,
};

// How particles cover what is behind them.
enum class ParticleBlend : std::uint8_t
{
    // By their alpha, drawn from the farthest to the nearest: smoke, dust, leaves.
    Alpha,
    // Adding their light, in any order: fire, sparks, magic.
    Additive,
};

// How a particle faces the camera.
enum class ParticleRenderMode : std::uint8_t
{
    Billboard,
    // Lengthened along its motion, as sparks and rain streak.
    Stretched,
    // Lying flat, facing up: ripples, glows on the ground.
    Horizontal,
    // Standing up, turned towards the camera around the vertical.
    Vertical,
};

// What makes a particle emit particles of another emitter.
enum class SubEmitTrigger : std::uint8_t
{
    Death,
    Collision,
};

// Emits and simulates particles: fire, smoke, sparks, dust, rain, magic. The fields are grouped as
// the inspector shows them. Ranges hold a minimum and a maximum, between which each particle draws
// its own value. Particles are not saved: a scene starts without them, and emitters that play on
// start fill up again.
struct DEVEX_API ParticleEmitter
{
    // Emitter.
    // Emits once the game, or the preview of the editor, starts; otherwise code plays it.
    bool playOnStart = true;
    // Starts a new cycle once the duration is over; otherwise stops emitting.
    bool looping = true;
    // Seconds of a cycle, whose start emits the burst.
    float duration = 5.0f;
    // Starts as if a cycle had already played, full of particles.
    bool prewarm = false;
    // The most particles alive at once; no more are emitted beyond.
    std::int32_t maxParticles = 1000;
    ParticleSpace space = ParticleSpace::World;
    // The same seed plays the same particles; 0 plays different ones every time.
    std::uint32_t seed = 0;

    // Emission.
    // Particles per second.
    float rate = 10.0f;
    // Particles per meter the emitter moves, for trails of dust.
    float rateOverDistance = 0.0f;
    // Particles emitted at once at the start of each cycle.
    std::int32_t burst = 0;

    // Shape.
    ParticleShape shape = ParticleShape::Cone;
    // Of the sphere, the circle and the base of the cone, in meters.
    float radius = 0.2f;
    // Between the axis of the cone and its side, in radians.
    float angle = 0.4363323f;
    math::Vec3 boxSize{1.0f};
    // From the surface of the shape rather than from inside it.
    bool fromShell = false;
    // From 0, the directions of the shape, to 1, any direction.
    float randomDirection = 0.0f;

    // Particles.
    // Seconds a particle lives.
    math::Vec2 lifetime{1.5f, 2.5f};
    // Meters per second at birth.
    math::Vec2 speed{1.0f, 2.0f};
    // Width in meters.
    math::Vec2 size{0.2f, 0.4f};
    // Turn at birth, and turning speed, in degrees and degrees per second.
    math::Vec2 rotation{0.0f, 360.0f};
    math::Vec2 spin{0.0f, 0.0f};
    // At birth and at death, blended over the life; alpha included.
    math::Vec4 color{1.0f};
    math::Vec4 endColor{1.0f};
    // Multiplies the color: above 1, particles glow and bloom.
    float intensity = 1.0f;

    // Motion.
    // Times the gravity of the world pulls down.
    float gravity = 0.0f;
    // A constant push, in meters per second squared: wind, buoyancy.
    math::Vec3 acceleration{0.0f};
    // How much speed particles lose per second, from 0 to a few.
    float drag = 0.0f;
    // The part of the motion of the emitter new particles take.
    float inheritVelocity = 0.0f;
    // A swirling push, in meters per second squared, that varies across space and time.
    float noise = 0.0f;
    // How close together the swirls are, per meter.
    float noiseFrequency = 1.0f;
    // How fast the swirls change, per second.
    float noiseSpeed = 0.5f;

    // Over life: curves from birth (0) to death (1), which multiply the value; none keeps it.
    asset::AssetId sizeCurve;
    asset::AssetId alphaCurve;
    asset::AssetId speedCurve;

    // Collision: particles bounce off the bodies of the physics while the game plays.
    bool collide = false;
    // The part of the speed kept across the surface they hit.
    float bounce = 0.3f;
    // The part of the speed along the surface they lose.
    float friction = 0.2f;
    // The part of their life a hit takes; 1 ends them where they land.
    float lifetimeLoss = 0.0f;

    // Sub emitter: another emitter, which only emits where these particles die or hit.
    EntityRef subEmitter;
    SubEmitTrigger subEmitOn = SubEmitTrigger::Death;
    std::int32_t subEmitCount = 8;

    // Rendering.
    // None draws a soft disc.
    asset::AssetId texture;
    ParticleBlend blend = ParticleBlend::Alpha;
    ParticleRenderMode renderMode = ParticleRenderMode::Billboard;
    // Stretched particles: how many seconds of their motion their length covers.
    float stretch = 0.05f;
    // A texture made of several frames side by side, played over the life of each particle.
    std::int32_t sheetColumns = 1;
    std::int32_t sheetRows = 1;
    // How many times the frames play over a life.
    float sheetCycles = 1.0f;
    // Each particle starts at a random frame and keeps it.
    bool randomFrame = false;
    // Lit by the sun, the sky and the lights as a matte surface; otherwise its color shines alone.
    bool lit = false;
    // Meters over which particles fade where they meet a surface, instead of cutting through it.
    float softness = 0.3f;

    // Trails: each particle leaves a ribbon behind it.
    bool trails = false;
    // Seconds the ribbon lasts behind the particle.
    float trailTime = 0.3f;
    // The width of the ribbon, as a part of the size of the particle.
    float trailWidth = 0.5f;
};
DEVEX_DECLARE_ENGINE_REFLECTION(ParticleEmitter);

// Leaves a ribbon behind its entity as it moves: the streak of a projectile, of a sword, of a
// thrown ball.
struct DEVEX_API TrailRenderer
{
    // Adds points while true; the ribbon still fades out when false.
    bool emitting = true;
    // Seconds a point of the ribbon lasts.
    float time = 0.5f;
    // Meters the entity moves before a new point is added.
    float minDistance = 0.05f;
    // Widths in meters at the entity and at the end of the ribbon.
    float width = 0.2f;
    float endWidth = 0.0f;
    // Colors at the entity and at the end of the ribbon, alpha included.
    math::Vec4 color{1.0f};
    math::Vec4 endColor{1.0f, 1.0f, 1.0f, 0.0f};
    float intensity = 1.0f;
    // Stretched along the ribbon; none draws it plain.
    asset::AssetId texture;
    ParticleBlend blend = ParticleBlend::Alpha;
    bool lit = false;
    float softness = 0.0f;
};
DEVEX_DECLARE_ENGINE_REFLECTION(TrailRenderer);

} // namespace devex::scene

template <>
struct devex::reflection::EnumNames<devex::scene::ParticleSpace>
{
    static constexpr std::array<std::string_view, 2> names{"world", "local"};
};

template <>
struct devex::reflection::EnumNames<devex::scene::ParticleShape>
{
    static constexpr std::array<std::string_view, 6> names{"point", "sphere", "hemisphere", "cone", "box", "circle"};
};

template <>
struct devex::reflection::EnumNames<devex::scene::ParticleBlend>
{
    static constexpr std::array<std::string_view, 2> names{"alpha", "additive"};
};

template <>
struct devex::reflection::EnumNames<devex::scene::ParticleRenderMode>
{
    static constexpr std::array<std::string_view, 4> names{"billboard", "stretched", "horizontal", "vertical"};
};

template <>
struct devex::reflection::EnumNames<devex::scene::SubEmitTrigger>
{
    static constexpr std::array<std::string_view, 2> names{"death", "collision"};
};
