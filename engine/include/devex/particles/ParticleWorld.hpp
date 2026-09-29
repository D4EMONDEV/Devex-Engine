#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/AssetId.hpp>
#include <devex/asset/CurveData.hpp>
#include <devex/core/Time.hpp>
#include <devex/math/Math.hpp>
#include <devex/scene/Entity.hpp>
#include <devex/scene/ParticleComponents.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace devex::core {
class JobSystem;
} // namespace devex::core

namespace devex::scene {
class Scene;
} // namespace devex::scene

namespace devex::particles {

// A particle as the simulation keeps it, in the space of its emitter: the world, or the emitter
// itself for emitters in local space.
struct DEVEX_API Particle
{
    math::Vec3 position{0.0f};
    // Seconds since its birth.
    float age = 0.0f;
    math::Vec3 velocity{0.0f};
    float lifetime = 1.0f;
    float startSize = 1.0f;
    // Now, with the curves of its life applied.
    float size = 1.0f;
    // In radians, and radians per second.
    float rotation = 0.0f;
    float spin = 0.0f;
    // Now, intensity included; alpha straight.
    math::Vec4 color{1.0f};
    // The frame of the sheet it shows.
    float frame = 0.0f;
    // Its own random number, which places its swirls and its frame.
    std::uint32_t random = 0;
    // Where its trail is kept, in EmitterState::trails, when the emitter draws trails.
    std::uint32_t trail = 0;
};

// A point of a trail, and when it was left there.
struct DEVEX_API TrailPoint
{
    math::Vec3 position{0.0f};
    float time = 0.0f;
};

// What the renderer needs of an emitter: its settings, where it stands and its particles.
struct DEVEX_API EmitterView
{
    scene::Entity entity;
    const scene::ParticleEmitter* settings = nullptr;
    // The world transform of the emitter, which places particles in local space.
    math::Mat4 transform{1.0f};
    std::span<const Particle> particles;
    // When the emitter draws trails: trailPointsPerParticle() points for each Particle::trail,
    // newest first, and how many of them each trail holds.
    std::span<const TrailPoint> trails;
    std::span<const std::uint8_t> trailCounts;
    // The time of the simulation, which ages the points of the trails.
    float time = 0.0f;
};

// Points each particle keeps of its trail, the particle itself being the head.
[[nodiscard]] DEVEX_API std::uint32_t trailPointsPerParticle() noexcept;

// What the renderer needs of a TrailRenderer: its settings and the points it left, newest first.
struct DEVEX_API TrailView
{
    scene::Entity entity;
    const scene::TrailRenderer* settings = nullptr;
    // Where the entity stands now: the head of the ribbon.
    math::Vec3 head{0.0f};
    std::span<const TrailPoint> points;
    float time = 0.0f;
};

// The particles of a scene: those of its ParticleEmitter components, simulated on the workers of
// the job system, and the ribbons of its TrailRenderer components. Emitters are found by their
// entity every frame and start by themselves; their particles and ribbons are kept here, not in the
// scene. The simulation runs once the world transforms of the frame are known.
class DEVEX_API ParticleWorld
{
public:
    // The curve of an asset, loaded once and shared; null when it cannot be loaded.
    using CurveSource = std::function<std::shared_ptr<const asset::CurveData>(asset::AssetId curve)>;

    // Where a particle moving from one point to another hits a surface, and the normal there.
    struct DEVEX_API Hit
    {
        math::Vec3 point{0.0f};
        math::Vec3 normal{0.0f, 1.0f, 0.0f};
    };
    // Called from several workers at once: it must only read the world it queries.
    using CollisionQuery = std::function<std::optional<Hit>(math::Vec3 from, math::Vec3 to)>;

    // Without a job system, emitters are simulated one after the other on the calling thread.
    explicit ParticleWorld(CurveSource curves = {}, core::JobSystem* jobs = nullptr);
    ~ParticleWorld();

    ParticleWorld(const ParticleWorld&) = delete;
    ParticleWorld& operator=(const ParticleWorld&) = delete;

    // Particles of emitters that collide hit what the query finds; none, they pass through.
    void setCollisionQuery(CollisionQuery query);
    // What pulls particles down, times the gravity of each emitter.
    void setGravity(math::Vec3 gravity) noexcept;

    // Once per frame, once the world transforms are up to date: emitters that appear start,
    // particles are born, move and die, and ribbons follow their entities.
    void update(scene::Scene& scene, core::Duration delta);

    // Starts a new cycle of the emitter, emitting again.
    void play(scene::Scene& scene, scene::Entity entity);
    // Stops emitting; the particles alive finish their life, or vanish with clear.
    void stop(scene::Entity entity, bool clear = false);
    // Holds the emitter and its particles where they are.
    void pause(scene::Entity entity);
    void resume(scene::Entity entity);
    // Emits particles at once, whether the emitter plays or not.
    void emit(scene::Scene& scene, scene::Entity entity, std::int32_t count);
    // While it emits, or particles it emitted are alive.
    [[nodiscard]] bool isPlaying(scene::Entity entity) const;
    [[nodiscard]] std::size_t particleCount(scene::Entity entity) const;
    [[nodiscard]] std::size_t particleCount() const noexcept;
    [[nodiscard]] std::size_t emitterCount() const noexcept;

    // Holds every emitter, as the editor does when the game pauses.
    void setPaused(bool paused) noexcept;
    [[nodiscard]] bool paused() const noexcept;
    // Forgets every particle and ribbon, as a new scene does.
    void clear();

    // For the renderer, after update.
    void forEachEmitter(const std::function<void(const EmitterView&)>& visit) const;
    void forEachTrail(const std::function<void(const TrailView&)>& visit) const;

private:
    struct EmitterState;
    struct TrailState;
    struct SubEmission;

    EmitterState* state(scene::Scene& scene, scene::Entity entity);
    void simulate(EmitterState& emitter, float seconds) const;
    // Along the path of the emitter over the frame with spread, as the rates emit; otherwise where
    // it stands, as bursts and code do.
    void emitParticles(EmitterState& emitter, std::int32_t count, float seconds, bool spread) const;
    void subEmit(scene::Scene& scene, EmitterState& source);

    CurveSource m_curves;
    core::JobSystem* m_jobs = nullptr;
    CollisionQuery m_collisions;
    math::Vec3 m_gravity{0.0f, -9.81f, 0.0f};
    std::unordered_map<std::uint64_t, std::unique_ptr<EmitterState>> m_emitters;
    std::unordered_map<std::uint64_t, std::unique_ptr<TrailState>> m_trails;
    float m_time = 0.0f;
    bool m_paused = false;
};

} // namespace devex::particles
