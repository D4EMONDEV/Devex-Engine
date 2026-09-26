#include <devex/particles/ParticleWorld.hpp>

#include <devex/core/JobSystem.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/Scene.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <numbers>
#include <utility>

namespace devex::particles {
namespace {

// Points each particle keeps of its trail; the particle itself is the head.
constexpr std::uint32_t trailCapacity = 8;
// Points a TrailRenderer keeps at most, whatever its time.
constexpr std::size_t maxRibbonPoints = 256;
// Steps of a prewarm, per second of the cycle it plays in advance.
constexpr float prewarmStep = 1.0f / 30.0f;
constexpr std::size_t maxPrewarmSteps = 600;

[[nodiscard]] std::uint64_t keyOf(scene::Entity entity) noexcept
{
    return (static_cast<std::uint64_t>(entity.generation) << 32) | entity.index;
}

[[nodiscard]] std::uint32_t hash(std::uint32_t value) noexcept
{
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

// Different particles every time a seed of 0 plays.
[[nodiscard]] std::uint64_t freshSeed() noexcept
{
    static std::atomic<std::uint64_t> counter{0x243F6A8885A308D3ULL};
    return counter.fetch_add(0x9E3779B97F4A7C15ULL, std::memory_order_relaxed);
}

// xorshift64*, enough for particles and cheap to keep one per emitter.
class Random
{
public:
    explicit Random(std::uint64_t seed = 1) noexcept
        : m_state(seed != 0 ? seed : 0x9E3779B97F4A7C15ULL)
    {
    }

    std::uint32_t next() noexcept
    {
        m_state ^= m_state >> 12;
        m_state ^= m_state << 25;
        m_state ^= m_state >> 27;
        return static_cast<std::uint32_t>((m_state * 0x2545F4914F6CDD1DULL) >> 32);
    }
    // In [0, 1).
    float unit() noexcept
    {
        return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f);
    }
    float range(math::Vec2 range) noexcept
    {
        return range.x + (range.y - range.x) * unit();
    }
    math::Vec3 direction() noexcept
    {
        const float z = unit() * 2.0f - 1.0f;
        const float angle = unit() * 2.0f * std::numbers::pi_v<float>;
        const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
        return {radius * std::cos(angle), radius * std::sin(angle), z};
    }
    // A point of the unit disc, spread evenly.
    math::Vec2 disc() noexcept
    {
        const float radius = std::sqrt(unit());
        const float angle = unit() * 2.0f * std::numbers::pi_v<float>;
        return {radius * std::cos(angle), radius * std::sin(angle)};
    }
    math::Vec2 circle() noexcept
    {
        const float angle = unit() * 2.0f * std::numbers::pi_v<float>;
        return {std::cos(angle), std::sin(angle)};
    }

private:
    std::uint64_t m_state;
};

// Gradient noise in three dimensions, from -1 to 1, smooth across space.
[[nodiscard]] float gradient(std::int32_t x, std::int32_t y, std::int32_t z, math::Vec3 offset) noexcept
{
    const std::uint32_t h = hash(static_cast<std::uint32_t>(x) * 73856093U ^ static_cast<std::uint32_t>(y) * 19349663U ^
                                 static_cast<std::uint32_t>(z) * 83492791U) %
                            12U;
    // The twelve edges of a cube, as Perlin's improved noise.
    const float u = h < 8 ? offset.x : offset.y;
    const float v = h < 4 ? offset.y : offset.z;
    return ((h & 1U) != 0 ? -u : u) + ((h & 2U) != 0 ? -v : v);
}

[[nodiscard]] float noise(math::Vec3 point) noexcept
{
    const math::Vec3 cell = math::floor(point);
    const math::Vec3 f = point - cell;
    const math::Vec3 w = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    const auto x = static_cast<std::int32_t>(cell.x);
    const auto y = static_cast<std::int32_t>(cell.y);
    const auto z = static_cast<std::int32_t>(cell.z);
    const auto corner = [&](std::int32_t dx, std::int32_t dy, std::int32_t dz) {
        return gradient(x + dx, y + dy, z + dz,
                        f - math::Vec3{static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz)});
    };
    const float x00 = std::lerp(corner(0, 0, 0), corner(1, 0, 0), w.x);
    const float x10 = std::lerp(corner(0, 1, 0), corner(1, 1, 0), w.x);
    const float x01 = std::lerp(corner(0, 0, 1), corner(1, 0, 1), w.x);
    const float x11 = std::lerp(corner(0, 1, 1), corner(1, 1, 1), w.x);
    return std::lerp(std::lerp(x00, x10, w.y), std::lerp(x01, x11, w.y), w.z);
}

// A push that swirls, different along each axis.
[[nodiscard]] math::Vec3 swirl(math::Vec3 point) noexcept
{
    return {noise(point), noise(point + math::Vec3{31.4f, 17.2f, 5.3f}), noise(point + math::Vec3{-11.7f, 43.1f, 27.9f})};
}

[[nodiscard]] float evaluate(const std::shared_ptr<const asset::CurveData>& curve, float time) noexcept
{
    return curve != nullptr ? curve->evaluate(time) : 1.0f;
}

} // namespace

struct ParticleWorld::SubEmission
{
    math::Vec3 position{0.0f};
    math::Vec3 velocity{0.0f};
};

struct ParticleWorld::EmitterState
{
    scene::Entity entity;
    scene::ParticleEmitter settings;
    math::Mat4 transform{1.0f};
    math::Vec3 position{0.0f};
    math::Vec3 previousPosition{0.0f};
    math::Vec3 velocity{0.0f};
    bool placed = false;
    std::shared_ptr<const asset::CurveData> sizeCurve;
    std::shared_ptr<const asset::CurveData> alphaCurve;
    std::shared_ptr<const asset::CurveData> speedCurve;
    std::vector<Particle> particles;
    // trailCapacity points per slot, newest first, and how many each slot holds.
    std::vector<TrailPoint> trails;
    std::vector<std::uint8_t> trailCounts;
    std::vector<std::uint32_t> freeTrails;
    Random random;
    // Seconds into the cycle.
    float cycle = 0.0f;
    // Parts of particles owed by the rates.
    float owed = 0.0f;
    float owedByDistance = 0.0f;
    std::int32_t pendingBurst = 0;
    std::int32_t pendingEmit = 0;
    bool emitting = false;
    bool prewarmPending = false;
    bool paused = false;
    bool seen = false;
    // What died or hit this frame, for the sub emitter.
    std::vector<SubEmission> subEmissions;
    float time = 0.0f;
};

struct ParticleWorld::TrailState
{
    scene::Entity entity;
    scene::TrailRenderer settings;
    math::Vec3 head{0.0f};
    // Newest first.
    std::vector<TrailPoint> points;
    bool seen = false;
};

ParticleWorld::ParticleWorld(CurveSource curves, core::JobSystem* jobs)
    : m_curves(std::move(curves))
    , m_jobs(jobs)
{
}

ParticleWorld::~ParticleWorld() = default;

void ParticleWorld::setCollisionQuery(CollisionQuery query)
{
    m_collisions = std::move(query);
}

void ParticleWorld::setGravity(math::Vec3 gravity) noexcept
{
    m_gravity = gravity;
}

namespace {

void startCycle(scene::ParticleEmitter& settings, float& cycle, float& owed, float& owedByDistance, std::int32_t& burst,
                bool& prewarm, bool& emitting)
{
    emitting = true;
    cycle = 0.0f;
    owed = 0.0f;
    owedByDistance = 0.0f;
    burst = std::max(settings.burst, 0);
    prewarm = settings.prewarm;
}

} // namespace

ParticleWorld::EmitterState* ParticleWorld::state(scene::Scene& scene, scene::Entity entity)
{
    if (!scene.isAlive(entity))
    {
        return nullptr;
    }
    const scene::ParticleEmitter* const settings = scene.tryGet<scene::ParticleEmitter>(entity);
    if (settings == nullptr)
    {
        return nullptr;
    }
    std::unique_ptr<EmitterState>& found = m_emitters[keyOf(entity)];
    if (!found)
    {
        found = std::make_unique<EmitterState>();
        found->entity = entity;
        found->settings = *settings;
        found->random = Random(settings->seed != 0 ? settings->seed : freshSeed());
        if (const scene::WorldTransform* const world = scene.tryGet<scene::WorldTransform>(entity))
        {
            found->transform = world->matrix;
            found->position = math::Vec3(world->matrix[3]);
        }
    }
    return found.get();
}

void ParticleWorld::update(scene::Scene& scene, core::Duration delta)
{
    const float seconds = m_paused ? 0.0f : std::max(static_cast<float>(delta.count()), 0.0f);
    m_time += seconds;

    for (auto& [key, emitter] : m_emitters)
    {
        emitter->seen = false;
    }
    std::vector<EmitterState*> running;
    for ([[maybe_unused]] auto [entity, settings] : scene.view<scene::ParticleEmitter>())
    {
        const bool fresh = !m_emitters.contains(keyOf(entity));
        EmitterState* const emitter = state(scene, entity);
        emitter->seen = true;
        emitter->settings = settings;
        if (const scene::WorldTransform* const world = scene.tryGet<scene::WorldTransform>(entity))
        {
            emitter->transform = world->matrix;
            emitter->position = math::Vec3(world->matrix[3]);
        }
        if (fresh && settings.playOnStart)
        {
            startCycle(emitter->settings, emitter->cycle, emitter->owed, emitter->owedByDistance, emitter->pendingBurst,
                       emitter->prewarmPending, emitter->emitting);
        }
        // The curves are found here, on the main thread, which loads them.
        emitter->sizeCurve = settings.sizeCurve.isValid() && m_curves ? m_curves(settings.sizeCurve) : nullptr;
        emitter->alphaCurve = settings.alphaCurve.isValid() && m_curves ? m_curves(settings.alphaCurve) : nullptr;
        emitter->speedCurve = settings.speedCurve.isValid() && m_curves ? m_curves(settings.speedCurve) : nullptr;
        emitter->time = m_time;
        if (!emitter->paused && !m_paused)
        {
            running.push_back(emitter);
        }
    }
    std::erase_if(m_emitters, [](const auto& entry) { return !entry.second->seen; });

    if (!running.empty())
    {
        if (m_jobs != nullptr && running.size() > 1)
        {
            m_jobs->parallelFor(running.size(), [&](std::size_t index) { simulate(*running[index], seconds); });
        }
        else
        {
            for (EmitterState* const emitter : running)
            {
                simulate(*emitter, seconds);
            }
        }
        for (EmitterState* const emitter : running)
        {
            if (!emitter->subEmissions.empty())
            {
                subEmit(scene, *emitter);
            }
        }
    }

    // Ribbons follow their entities, a point every few centimeters.
    for (auto& [key, trail] : m_trails)
    {
        trail->seen = false;
    }
    for ([[maybe_unused]] auto [entity, settings, world] : scene.view<scene::TrailRenderer, scene::WorldTransform>())
    {
        std::unique_ptr<TrailState>& trail = m_trails[keyOf(entity)];
        if (!trail)
        {
            trail = std::make_unique<TrailState>();
            trail->entity = entity;
        }
        trail->seen = true;
        trail->settings = settings;
        trail->head = math::Vec3(world.matrix[3]);
        if (m_paused)
        {
            continue;
        }
        std::vector<TrailPoint>& points = trail->points;
        const float spacing = std::max(settings.minDistance, 0.001f);
        if (settings.emitting &&
            (points.empty() || math::distance(points.front().position, trail->head) >= spacing))
        {
            points.insert(points.begin(), TrailPoint{.position = trail->head, .time = m_time});
        }
        const float life = std::max(settings.time, 0.0f);
        while (!points.empty() && (m_time - points.back().time > life || points.size() > maxRibbonPoints))
        {
            points.pop_back();
        }
    }
    std::erase_if(m_trails, [](const auto& entry) { return !entry.second->seen; });
}

void ParticleWorld::simulate(EmitterState& emitter, float seconds) const
{
    const scene::ParticleEmitter& settings = emitter.settings;
    if (!emitter.placed)
    {
        emitter.previousPosition = emitter.position;
        emitter.placed = true;
    }
    emitter.velocity = seconds > 0.0f ? (emitter.position - emitter.previousPosition) / seconds : math::Vec3{0.0f};

    if (emitter.prewarmPending)
    {
        // As if a whole cycle had played already, the emitter standing still.
        emitter.prewarmPending = false;
        const math::Vec3 velocity = emitter.velocity;
        emitter.velocity = math::Vec3{0.0f};
        emitter.previousPosition = emitter.position;
        const auto steps = std::min(static_cast<std::size_t>(std::max(settings.duration, 0.0f) / prewarmStep),
                                    maxPrewarmSteps);
        for (std::size_t step = 0; step < steps; ++step)
        {
            simulate(emitter, prewarmStep);
        }
        emitter.subEmissions.clear();
        emitter.velocity = velocity;
    }

    const bool local = settings.space == scene::ParticleSpace::Local;
    const math::Mat3 basis(emitter.transform);
    const math::Mat3 inverseBasis = math::inverse(basis);
    const math::Vec3 gravity = (local ? inverseBasis * m_gravity : m_gravity) * settings.gravity;
    const math::Vec3 acceleration = local ? inverseBasis * settings.acceleration : settings.acceleration;
    const float drag = std::max(1.0f - std::max(settings.drag, 0.0f) * seconds, 0.0f);
    const bool collides = settings.collide && m_collisions;
    const bool swirls = settings.noise != 0.0f;
    const bool trails = settings.trails;
    const float trailSpacing = std::max(settings.trailTime, 0.01f) / static_cast<float>(trailCapacity);
    const std::int32_t frames = std::max(settings.sheetColumns, 1) * std::max(settings.sheetRows, 1);
    const bool subOnDeath = !settings.subEmitter.isNil() && settings.subEmitOn == scene::SubEmitTrigger::Death;
    const bool subOnHit = !settings.subEmitter.isNil() && settings.subEmitOn == scene::SubEmitTrigger::Collision;
    const auto toWorld = [&](math::Vec3 point) {
        return local ? math::Vec3(emitter.transform * math::Vec4(point, 1.0f)) : point;
    };

    std::vector<Particle>& particles = emitter.particles;
    for (std::size_t index = 0; index < particles.size();)
    {
        Particle& particle = particles[index];
        particle.age += seconds;
        if (particle.age < particle.lifetime)
        {
            const float life = particle.age / particle.lifetime;
            math::Vec3 force = gravity + acceleration;
            if (swirls)
            {
                const math::Vec3 at = particle.position * settings.noiseFrequency +
                                      math::Vec3{1.0f, 0.7f, 0.4f} * (emitter.time * settings.noiseSpeed);
                force += swirl(at) * settings.noise;
            }
            particle.velocity = (particle.velocity + force * seconds) * drag;
            const math::Vec3 next = particle.position + particle.velocity * (evaluate(emitter.speedCurve, life) * seconds);
            math::Vec3 landed = next;
            if (collides && seconds > 0.0f)
            {
                if (const std::optional<Hit> hit = m_collisions(toWorld(particle.position), toWorld(next)))
                {
                    const math::Vec3 normal = local ? math::normalize(inverseBasis * hit->normal) : hit->normal;
                    const math::Vec3 across = normal * math::dot(particle.velocity, normal);
                    const math::Vec3 along = particle.velocity - across;
                    particle.velocity = along * (1.0f - settings.friction) - across * settings.bounce;
                    const math::Vec3 point = hit->point + hit->normal * 0.01f;
                    landed = local ? math::Vec3(math::inverse(emitter.transform) * math::Vec4(point, 1.0f)) : point;
                    particle.age += particle.lifetime * std::max(settings.lifetimeLoss, 0.0f);
                    if (subOnHit)
                    {
                        emitter.subEmissions.push_back({.position = point, .velocity = local ? basis * particle.velocity : particle.velocity});
                    }
                }
            }
            particle.position = landed;
            particle.rotation += particle.spin * seconds;
            if (trails)
            {
                TrailPoint* const points = &emitter.trails[static_cast<std::size_t>(particle.trail) * trailCapacity];
                std::uint8_t& count = emitter.trailCounts[particle.trail];
                if (count == 0 || emitter.time - points[0].time >= trailSpacing)
                {
                    std::shift_right(points, points + trailCapacity, 1);
                    points[0] = {.position = particle.position, .time = emitter.time};
                    count = static_cast<std::uint8_t>(std::min<std::uint32_t>(count + 1U, trailCapacity));
                }
            }
        }
        if (particle.age >= particle.lifetime)
        {
            if (subOnDeath)
            {
                emitter.subEmissions.push_back({.position = toWorld(particle.position),
                                                .velocity = local ? basis * particle.velocity : particle.velocity});
            }
            if (trails)
            {
                emitter.trailCounts[particle.trail] = 0;
                emitter.freeTrails.push_back(particle.trail);
            }
            particle = particles.back();
            particles.pop_back();
            continue;
        }
        const float life = particle.age / particle.lifetime;
        particle.size = particle.startSize * std::max(evaluate(emitter.sizeCurve, life), 0.0f);
        math::Vec4 color = settings.color + (settings.endColor - settings.color) * life;
        color = math::Vec4(math::Vec3(color) * settings.intensity, std::max(color.a * evaluate(emitter.alphaCurve, life), 0.0f));
        particle.color = color;
        if (!settings.randomFrame && frames > 1)
        {
            const float played = life * std::max(settings.sheetCycles, 0.0f) * static_cast<float>(frames);
            particle.frame = std::fmod(std::floor(played), static_cast<float>(frames));
        }
        ++index;
    }

    // What the emitter owes this frame: its bursts and what code asked for, where it stands, and
    // its rates, along its way.
    std::int32_t count = std::exchange(emitter.pendingEmit, 0);
    std::int32_t spread = 0;
    if (emitter.emitting)
    {
        count += std::exchange(emitter.pendingBurst, 0);
        emitter.owed += std::max(settings.rate, 0.0f) * seconds;
        emitter.owedByDistance +=
            std::max(settings.rateOverDistance, 0.0f) * math::distance(emitter.position, emitter.previousPosition);
        const auto owed = static_cast<std::int32_t>(emitter.owed);
        const auto owedByDistance = static_cast<std::int32_t>(emitter.owedByDistance);
        emitter.owed -= static_cast<float>(owed);
        emitter.owedByDistance -= static_cast<float>(owedByDistance);
        spread = owed + owedByDistance;
        emitter.cycle += seconds;
        const float duration = std::max(settings.duration, 0.01f);
        while (emitter.cycle >= duration && emitter.emitting)
        {
            if (settings.looping)
            {
                emitter.cycle -= duration;
                count += std::max(settings.burst, 0);
            }
            else
            {
                emitter.emitting = false;
            }
        }
    }
    emitParticles(emitter, count, 0.0f, false);
    emitParticles(emitter, spread, seconds, true);
    emitter.previousPosition = emitter.position;
}

void ParticleWorld::emitParticles(EmitterState& emitter, std::int32_t count, float seconds, bool spread) const
{
    const scene::ParticleEmitter& settings = emitter.settings;
    const auto capacity = static_cast<std::size_t>(std::max(settings.maxParticles, 0));
    const bool local = settings.space == scene::ParticleSpace::Local;
    const math::Mat3 basis(emitter.transform);
    const std::int32_t frames = std::max(settings.sheetColumns, 1) * std::max(settings.sheetRows, 1);
    Random& random = emitter.random;
    for (std::int32_t index = 0; index < count && emitter.particles.size() < capacity; ++index)
    {
        // Spread along the path of the emitter over the frame, the first ones older.
        const float fraction =
            spread && count > 1 ? (static_cast<float>(index) + 0.5f) / static_cast<float>(count) : 1.0f;

        math::Vec3 position{0.0f};
        math::Vec3 direction{0.0f, 0.0f, -1.0f};
        const float radius = std::max(settings.radius, 0.0f);
        switch (settings.shape)
        {
        case scene::ParticleShape::Point:
            direction = random.direction();
            break;
        case scene::ParticleShape::Sphere:
        case scene::ParticleShape::Hemisphere: {
            direction = random.direction();
            if (settings.shape == scene::ParticleShape::Hemisphere && direction.z > 0.0f)
            {
                direction.z = -direction.z;
            }
            const float distance = settings.fromShell ? radius : radius * std::cbrt(random.unit());
            position = direction * distance;
            break;
        }
        case scene::ParticleShape::Cone: {
            const math::Vec2 point = settings.fromShell ? random.circle() : random.disc();
            position = math::Vec3(point * radius, 0.0f);
            const float tilt = std::sin(settings.angle);
            direction = math::normalize(math::Vec3(point * tilt, -std::cos(settings.angle)));
            break;
        }
        case scene::ParticleShape::Box: {
            const math::Vec3 half = settings.boxSize * 0.5f;
            position = math::Vec3{random.unit() * 2.0f - 1.0f, random.unit() * 2.0f - 1.0f, random.unit() * 2.0f - 1.0f} * half;
            if (settings.fromShell)
            {
                // Pushed onto the face its largest coordinate points to.
                const math::Vec3 scaled = math::abs(position / math::max(half, math::Vec3{1e-5f}));
                const int axis = scaled.x >= scaled.y && scaled.x >= scaled.z ? 0 : (scaled.y >= scaled.z ? 1 : 2);
                position[axis] = position[axis] >= 0.0f ? half[axis] : -half[axis];
            }
            break;
        }
        case scene::ParticleShape::Circle: {
            const math::Vec2 point = settings.fromShell ? random.circle() : random.disc();
            position = math::Vec3(point * radius, 0.0f);
            const float length = math::length(point);
            direction = length > 1e-5f ? math::Vec3(point / length, 0.0f) : math::Vec3(random.circle(), 0.0f);
            break;
        }
        }
        if (settings.randomDirection > 0.0f)
        {
            const math::Vec3 any = random.direction();
            const math::Vec3 blended = direction + (any - direction) * std::clamp(settings.randomDirection, 0.0f, 1.0f);
            direction = math::length(blended) > 1e-5f ? math::normalize(blended) : any;
        }

        Particle particle;
        if (local)
        {
            particle.position = position;
            particle.velocity = direction * random.range(settings.speed);
        }
        else
        {
            const math::Vec3 origin = emitter.previousPosition + (emitter.position - emitter.previousPosition) * fraction;
            particle.position = basis * position + origin;
            const math::Vec3 turned = basis * direction;
            const float length = math::length(turned);
            particle.velocity = (length > 1e-6f ? turned / length : direction) * random.range(settings.speed) +
                                emitter.velocity * settings.inheritVelocity;
        }
        particle.lifetime = std::max(random.range(settings.lifetime), 0.01f);
        particle.startSize = std::max(random.range(settings.size), 0.0f);
        particle.rotation = math::radians(random.range(settings.rotation));
        particle.spin = math::radians(random.range(settings.spin));
        particle.random = random.next();
        particle.frame = settings.randomFrame ? static_cast<float>(particle.random % static_cast<std::uint32_t>(frames)) : 0.0f;
        // Born during the frame: already on its way.
        particle.age = (1.0f - fraction) * seconds;
        particle.position += particle.velocity * particle.age;
        const float life = particle.age / particle.lifetime;
        particle.size = particle.startSize * std::max(evaluate(emitter.sizeCurve, life), 0.0f);
        const math::Vec4 color = settings.color + (settings.endColor - settings.color) * life;
        particle.color = math::Vec4(math::Vec3(color) * settings.intensity,
                                    std::max(color.a * evaluate(emitter.alphaCurve, life), 0.0f));
        if (settings.trails)
        {
            if (emitter.freeTrails.empty())
            {
                particle.trail = static_cast<std::uint32_t>(emitter.trailCounts.size());
                emitter.trailCounts.push_back(0);
                emitter.trails.resize(emitter.trailCounts.size() * trailCapacity);
            }
            else
            {
                particle.trail = emitter.freeTrails.back();
                emitter.freeTrails.pop_back();
                emitter.trailCounts[particle.trail] = 0;
            }
        }
        emitter.particles.push_back(particle);
    }
}

void ParticleWorld::subEmit(scene::Scene& scene, EmitterState& source)
{
    std::vector<SubEmission> events = std::exchange(source.subEmissions, {});
    const scene::Entity target = scene.resolve(source.settings.subEmitter);
    EmitterState* const emitter = target.isValid() && target != source.entity ? state(scene, target) : nullptr;
    if (emitter == nullptr)
    {
        return;
    }
    const bool local = emitter->settings.space == scene::ParticleSpace::Local;
    const math::Mat4 inverse = math::inverse(emitter->transform);
    // Each event emits from where it happened, as if the emitter stood there for an instant.
    const math::Mat4 transform = emitter->transform;
    const math::Vec3 position = emitter->position;
    const math::Vec3 previous = emitter->previousPosition;
    const math::Vec3 velocity = emitter->velocity;
    for (const SubEmission& event : events)
    {
        const math::Vec3 at = local ? math::Vec3(inverse * math::Vec4(event.position, 1.0f)) : event.position;
        emitter->transform[3] = local ? transform[3] : math::Vec4(at, 1.0f);
        emitter->position = at;
        emitter->previousPosition = at;
        emitter->velocity = event.velocity;
        const std::size_t first = emitter->particles.size();
        emitParticles(*emitter, std::max(source.settings.subEmitCount, 0), 0.0f, false);
        if (local)
        {
            for (std::size_t index = first; index < emitter->particles.size(); ++index)
            {
                emitter->particles[index].position += at;
            }
        }
    }
    emitter->transform = transform;
    emitter->position = position;
    emitter->previousPosition = previous;
    emitter->velocity = velocity;
}

void ParticleWorld::play(scene::Scene& scene, scene::Entity entity)
{
    if (EmitterState* const emitter = state(scene, entity))
    {
        emitter->settings = scene.get<scene::ParticleEmitter>(entity);
        if (emitter->settings.seed != 0)
        {
            emitter->random = Random(emitter->settings.seed);
        }
        emitter->paused = false;
        startCycle(emitter->settings, emitter->cycle, emitter->owed, emitter->owedByDistance, emitter->pendingBurst,
                   emitter->prewarmPending, emitter->emitting);
    }
}

void ParticleWorld::stop(scene::Entity entity, bool clear)
{
    const auto found = m_emitters.find(keyOf(entity));
    if (found == m_emitters.end())
    {
        return;
    }
    EmitterState& emitter = *found->second;
    emitter.emitting = false;
    emitter.pendingBurst = 0;
    emitter.pendingEmit = 0;
    if (clear)
    {
        emitter.particles.clear();
        emitter.trails.clear();
        emitter.trailCounts.clear();
        emitter.freeTrails.clear();
    }
}

void ParticleWorld::pause(scene::Entity entity)
{
    if (const auto found = m_emitters.find(keyOf(entity)); found != m_emitters.end())
    {
        found->second->paused = true;
    }
}

void ParticleWorld::resume(scene::Entity entity)
{
    if (const auto found = m_emitters.find(keyOf(entity)); found != m_emitters.end())
    {
        found->second->paused = false;
    }
}

void ParticleWorld::emit(scene::Scene& scene, scene::Entity entity, std::int32_t count)
{
    if (EmitterState* const emitter = state(scene, entity); emitter != nullptr && count > 0)
    {
        emitter->pendingEmit += count;
    }
}

bool ParticleWorld::isPlaying(scene::Entity entity) const
{
    const auto found = m_emitters.find(keyOf(entity));
    return found != m_emitters.end() && (found->second->emitting || !found->second->particles.empty() ||
                                         found->second->pendingEmit > 0);
}

std::size_t ParticleWorld::particleCount(scene::Entity entity) const
{
    const auto found = m_emitters.find(keyOf(entity));
    return found != m_emitters.end() ? found->second->particles.size() : 0;
}

std::size_t ParticleWorld::particleCount() const noexcept
{
    std::size_t count = 0;
    for (const auto& [key, emitter] : m_emitters)
    {
        count += emitter->particles.size();
    }
    return count;
}

std::size_t ParticleWorld::emitterCount() const noexcept
{
    return m_emitters.size();
}

void ParticleWorld::setPaused(bool paused) noexcept
{
    m_paused = paused;
}

bool ParticleWorld::paused() const noexcept
{
    return m_paused;
}

void ParticleWorld::clear()
{
    m_emitters.clear();
    m_trails.clear();
}

void ParticleWorld::forEachEmitter(const std::function<void(const EmitterView&)>& visit) const
{
    for (const auto& [key, emitter] : m_emitters)
    {
        if (emitter->particles.empty())
        {
            continue;
        }
        visit(EmitterView{
            .entity = emitter->entity,
            .settings = &emitter->settings,
            .transform = emitter->transform,
            .particles = emitter->particles,
            .trails = emitter->trails,
            .trailCounts = emitter->trailCounts,
            .time = m_time,
        });
    }
}

void ParticleWorld::forEachTrail(const std::function<void(const TrailView&)>& visit) const
{
    for (const auto& [key, trail] : m_trails)
    {
        if (trail->points.empty())
        {
            continue;
        }
        visit(TrailView{
            .entity = trail->entity,
            .settings = &trail->settings,
            .head = trail->head,
            .points = trail->points,
            .time = m_time,
        });
    }
}

std::uint32_t trailPointsPerParticle() noexcept
{
    return trailCapacity;
}

} // namespace devex::particles
