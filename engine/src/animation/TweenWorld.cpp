#include <devex/animation/TweenWorld.hpp>

#include <devex/core/Log.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/InstanceShaderParameters.hpp>
#include <devex/scene/Scene.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>

namespace devex::animation {
namespace {

using reflection::ValueKind;

// Never the same twice in a process, so that a handle kept from a previous scene names nothing.
[[nodiscard]] std::uint64_t nextTweenId() noexcept
{
    static std::atomic<std::uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

[[nodiscard]] bool isAnimatable(ValueKind kind) noexcept
{
    return kind == ValueKind::Float || kind == ValueKind::Vec2 || kind == ValueKind::Vec3 || kind == ValueKind::Vec4 ||
           kind == ValueKind::Quat;
}

// The value of the field in four components; a rotation as its quaternion, x, y, z, w.
[[nodiscard]] math::Vec4 readValue(ValueKind kind, const void* address) noexcept
{
    switch (kind)
    {
    case ValueKind::Float:
        return {*static_cast<const float*>(address), 0.0f, 0.0f, 0.0f};
    case ValueKind::Vec2:
        return {*static_cast<const math::Vec2*>(address), 0.0f, 0.0f};
    case ValueKind::Vec3:
        return {*static_cast<const math::Vec3*>(address), 0.0f};
    case ValueKind::Vec4:
        return *static_cast<const math::Vec4*>(address);
    case ValueKind::Quat: {
        const math::Quat rotation = *static_cast<const math::Quat*>(address);
        return {rotation.x, rotation.y, rotation.z, rotation.w};
    }
    default:
        return math::Vec4{0.0f};
    }
}

void writeValue(ValueKind kind, void* address, math::Vec4 value) noexcept
{
    switch (kind)
    {
    case ValueKind::Float:
        *static_cast<float*>(address) = value.x;
        break;
    case ValueKind::Vec2:
        *static_cast<math::Vec2*>(address) = math::Vec2{value};
        break;
    case ValueKind::Vec3:
        *static_cast<math::Vec3*>(address) = math::Vec3{value};
        break;
    case ValueKind::Vec4:
        *static_cast<math::Vec4*>(address) = value;
        break;
    default:
        break;
    }
}

[[nodiscard]] math::Quat fromDegrees(math::Vec4 angles) noexcept
{
    return math::quatFromEulerAngles(math::radians(math::Vec3{angles}));
}

// "Component.field" split in two.
[[nodiscard]] std::pair<std::string_view, std::string_view> splitField(std::string_view path) noexcept
{
    const std::size_t dot = path.find('.');
    return dot == std::string_view::npos ? std::pair{path, std::string_view{}}
                                         : std::pair{path.substr(0, dot), path.substr(dot + 1)};
}

// "instance_shader_parameters/flash" names the instance uniform flash.
[[nodiscard]] std::string_view uniformOf(std::string_view fieldName) noexcept
{
    const std::string_view field = scene::instanceShaderParametersField;
    return fieldName.size() > field.size() + 1 && fieldName.starts_with(field) && fieldName[field.size()] == '/'
               ? fieldName.substr(field.size() + 1)
               : std::string_view{};
}

} // namespace

TweenWorld::TweenWorld(CurveSource curves, ShaderDefaults shaderDefaults)
    : m_curves(std::move(curves))
    , m_shaderDefaults(std::move(shaderDefaults))
{
}

TweenWorld::~TweenWorld() = default;

core::Result<TweenWorld::Target> TweenWorld::resolve(scene::Scene& scene, scene::Entity entity, std::string_view field) const
{
    const auto [componentName, fieldName] = splitField(field);
    const scene::ComponentType* const component = scene::componentRegistry().find(componentName);
    if (component == nullptr)
    {
        return core::makeError(core::ErrorCode::NotFound, "no component {} for '{}'", componentName, field);
    }
    if (const std::string_view uniform = uniformOf(fieldName); !uniform.empty())
    {
        void* const found = scene.isAlive(entity) ? component->findMutable(scene, entity) : nullptr;
        if (found == nullptr)
        {
            return core::makeError(core::ErrorCode::NotFound, "the entity has no {}", componentName);
        }
        if (!scene::instanceShaderValues(*component, found).isValid())
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "{} gives no instance uniforms to a shader", componentName);
        }
        return Target{.component = component, .uniform = std::string(uniform)};
    }
    const reflection::FieldInfo* const info = component->type->findField(fieldName);
    if (info == nullptr || info->list != nullptr || !isAnimatable(info->kind))
    {
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "{} has no field {} holding a number, a vector, a color or a rotation", componentName, fieldName);
    }
    if (!scene.isAlive(entity) || component->find(scene, entity) == nullptr)
    {
        return core::makeError(core::ErrorCode::NotFound, "the entity has no {}", componentName);
    }
    return Target{component, info};
}

core::Result<std::uint64_t> TweenWorld::start(scene::Scene& scene, const TweenSpec& tween, bool fromTweener)
{
    if (core::Result<Target> target = resolve(scene, tween.entity, tween.field); !target)
    {
        return std::unexpected(target.error());
    }
    const std::uint64_t id = nextTweenId();
    Tween started{
        .entity = scene.uuid(tween.entity),
        .spec = tween,
        .repeatsLeft = tween.loop == scene::TweenLoop::None ? 0 : tween.repeats,
        .fromTweener = fromTweener,
    };
    started.spec.duration = std::max(tween.duration, 0.0f);
    started.spec.delay = std::max(tween.delay, 0.0f);
    m_tweens.emplace(id, std::move(started));
    return id;
}

core::Result<TweenHandle> TweenWorld::play(scene::Scene& scene, const TweenSpec& tween)
{
    core::Result<std::uint64_t> id = start(scene, tween, false);
    if (!id)
    {
        return std::unexpected(id.error());
    }
    return TweenHandle{*id};
}

core::Result<TweenHandle> TweenWorld::playSequence(scene::Scene& scene, std::vector<SequenceStep> steps)
{
    // Every tween of every step names what it animates, which is checked now rather than later.
    for (const SequenceStep& step : steps)
    {
        for (const TweenSpec& tween : step.tweens)
        {
            if (core::Result<Target> target = resolve(scene, tween.entity, tween.field); !target)
            {
                return std::unexpected(target.error());
            }
        }
    }
    const std::uint64_t id = nextTweenId();
    Sequence& sequence = m_sequences.emplace(id, Sequence{.steps = std::move(steps)}).first->second;
    if (!advance(scene, sequence))
    {
        m_sequences.erase(id);
    }
    return TweenHandle{id};
}

core::Result<TweenHandle> TweenWorld::playTweener(scene::Scene& scene, scene::Entity entity)
{
    const scene::Tweener* const tweener = scene.isAlive(entity) ? scene.tryGet<scene::Tweener>(entity) : nullptr;
    if (tweener == nullptr)
    {
        return core::makeError(core::ErrorCode::NotFound, "the entity has no Tweener");
    }
    const core::Uuid uuid = scene.uuid(entity);
    if (const auto running = m_tweeners.find(uuid); running != m_tweeners.end())
    {
        m_tweens.erase(running->second);
    }
    const TweenSpec spec{
        .entity = entity,
        .field = tweener->field,
        .to = tweener->to,
        .from = tweener->fromCurrent ? std::nullopt : std::optional(tweener->from),
        .relative = tweener->relative,
        .duration = tweener->duration,
        .delay = tweener->delay,
        .ease = tweener->ease,
        .curve = tweener->curve,
        .loop = tweener->loop,
        .repeats = tweener->repeats,
    };
    core::Result<std::uint64_t> id = start(scene, spec, true);
    if (!id)
    {
        return std::unexpected(id.error());
    }
    m_tweeners.insert_or_assign(uuid, *id);
    return TweenHandle{*id};
}

bool TweenWorld::write(scene::Scene& scene, Tween& tween, float progress) const
{
    const scene::Entity entity = scene.findEntity(tween.entity);
    if (!entity.isValid() || (tween.fromTweener && !scene.has<scene::Tweener>(entity)))
    {
        return false;
    }
    const core::Result<Target> target = resolve(scene, entity, tween.spec.field);
    void* const component = target ? target->component->findMutable(scene, entity) : nullptr;
    if (component == nullptr)
    {
        return false;
    }
    if (!target->uniform.empty())
    {
        return writeUniform(*target, component, tween, progress);
    }
    const reflection::FieldInfo& field = *target->field;
    void* const address = field.address(component);

    if (!tween.started)
    {
        // The start is taken once the delay is over, from the value the field has then.
        tween.started = true;
        const math::Vec4 current = readValue(field.kind, address);
        if (field.kind == ValueKind::Quat)
        {
            const math::Quat now{current.w, current.x, current.y, current.z};
            tween.startRotation = tween.spec.from ? fromDegrees(*tween.spec.from) : now;
            const math::Quat turn = fromDegrees(tween.spec.to);
            tween.endRotation = tween.spec.relative ? math::normalize(tween.startRotation * turn) : turn;
        }
        else
        {
            tween.start = tween.spec.from.value_or(current);
            tween.end = tween.spec.relative ? tween.start + tween.spec.to : tween.spec.to;
        }
    }

    const float eased = easedProgress(tween, progress);
    if (field.kind == ValueKind::Quat)
    {
        *static_cast<math::Quat*>(address) = math::normalize(math::slerp(tween.startRotation, tween.endRotation, eased));
    }
    else
    {
        writeValue(field.kind, address, tween.start + (tween.end - tween.start) * eased);
    }
    return true;
}

float TweenWorld::easedProgress(const Tween& tween, float progress) const
{
    if (tween.spec.curve.isValid() && m_curves)
    {
        if (const std::shared_ptr<const asset::CurveData> curve = m_curves(tween.spec.curve))
        {
            return curve->evaluate(progress);
        }
    }
    return math::ease(tween.spec.ease, progress);
}

bool TweenWorld::writeUniform(const Target& target, void* component, Tween& tween, float progress) const
{
    const scene::InstanceShaderValues values = scene::instanceShaderValues(*target.component, component);
    if (!values.isValid())
    {
        return false;
    }
    if (!tween.started)
    {
        // A uniform the renderer gives no value yet starts from the default of its shader.
        tween.started = true;
        std::optional<math::Vec4> current = values.find(target.uniform);
        if (!current && m_shaderDefaults)
        {
            const reflection::FieldInfo* const material = target.component->type->findField("material");
            if (material != nullptr && material->kind == ValueKind::AssetId && material->list == nullptr)
            {
                current = m_shaderDefaults(*static_cast<const asset::AssetId*>(material->address(component)), target.uniform);
            }
        }
        tween.start = tween.spec.from.value_or(current.value_or(math::Vec4{0.0f}));
        tween.end = tween.spec.relative ? tween.start + tween.spec.to : tween.spec.to;
    }
    const float eased = easedProgress(tween, progress);
    values.set(target.uniform, tween.start + (tween.end - tween.start) * eased);
    return true;
}

bool TweenWorld::advance(scene::Scene& scene, Sequence& sequence)
{
    while (sequence.next < sequence.steps.size())
    {
        const SequenceStep& step = sequence.steps[sequence.next++];
        sequence.running.clear();
        sequence.waitLeft = -1.0f;
        for (const TweenSpec& tween : step.tweens)
        {
            if (core::Result<std::uint64_t> id = start(scene, tween, false))
            {
                sequence.running.push_back(*id);
            }
            else
            {
                DEVEX_LOG_WARNING("A step of a sequence is skipped: {}", id.error());
            }
        }
        if (!sequence.running.empty())
        {
            return true;
        }
        // A step of waiting only.
        if (step.interval > 0.0f)
        {
            sequence.waitLeft = step.interval;
            return true;
        }
    }
    return false;
}

void TweenWorld::update(scene::Scene& scene, core::Duration delta)
{
    // A Tweener removed starts over when it comes back.
    std::erase_if(m_tweeners, [&scene](const auto& tweener) {
        const scene::Entity entity = scene.findEntity(tweener.first);
        return !entity.isValid() || !scene.has<scene::Tweener>(entity);
    });
    // Tweeners that appear start by themselves.
    for ([[maybe_unused]] auto [entity, tweener] : scene.view<scene::Tweener>())
    {
        const core::Uuid uuid = scene.uuid(entity);
        if (tweener.playOnStart && !m_tweeners.contains(uuid))
        {
            if (core::Result<TweenHandle> started = playTweener(scene, entity); !started)
            {
                DEVEX_LOG_WARNING("The Tweener of {} does not play: {}", scene.name(entity), started.error());
                // Tried once, not every frame.
                m_tweeners.emplace(uuid, 0);
            }
        }
    }
    if (m_paused)
    {
        return;
    }

    const auto seconds = static_cast<float>(delta.count());
    std::vector<std::uint64_t> finished;
    for (auto& [id, tween] : m_tweens)
    {
        if (tween.completing)
        {
            tween.forward = true;
            static_cast<void>(write(scene, tween, 1.0f));
            finished.push_back(id);
            continue;
        }
        if (tween.paused)
        {
            continue;
        }
        tween.elapsed += seconds;
        const TweenSpec& spec = tween.spec;
        if (tween.elapsed < spec.delay)
        {
            continue;
        }
        float time = spec.duration > 0.0f ? (tween.elapsed - spec.delay) / spec.duration : 1.0f;
        bool done = false;
        while (time >= 1.0f && !done)
        {
            if (spec.loop == scene::TweenLoop::None || tween.repeatsLeft == 0)
            {
                time = 1.0f;
                done = true;
                break;
            }
            // Once more: from the start again, or back the other way.
            if (tween.repeatsLeft > 0)
            {
                --tween.repeatsLeft;
            }
            if (spec.loop == scene::TweenLoop::PingPong)
            {
                tween.forward = !tween.forward;
            }
            tween.elapsed -= spec.duration;
            time = spec.duration > 0.0f ? (tween.elapsed - spec.delay) / spec.duration : 0.0f;
            if (spec.duration <= 0.0f)
            {
                break;
            }
        }
        const float progress = tween.forward ? time : 1.0f - time;
        if (!write(scene, tween, std::clamp(progress, 0.0f, 1.0f)) || done)
        {
            finished.push_back(id);
        }
    }
    for (const std::uint64_t id : finished)
    {
        m_tweens.erase(id);
    }

    // The time of a frame goes to the tweens of a step or to a wait, never to both: the wait after
    // a step starts with the next frame, and a step starts playing with the next frame too.
    for (auto sequence = m_sequences.begin(); sequence != m_sequences.end();)
    {
        Sequence& current = sequence->second;
        bool next = false;
        if (!current.paused && current.waitLeft < 0.0f)
        {
            const bool stepPlaying =
                std::ranges::any_of(current.running, [this](std::uint64_t id) { return m_tweens.contains(id); });
            if (!stepPlaying)
            {
                current.running.clear();
                current.waitLeft = current.steps[current.next - 1].interval;
                next = current.waitLeft <= 0.0f;
            }
        }
        else if (!current.paused)
        {
            current.waitLeft -= seconds;
            next = current.waitLeft <= 0.0f;
        }
        if (next && !advance(scene, current))
        {
            sequence = m_sequences.erase(sequence);
            continue;
        }
        ++sequence;
    }
}

void TweenWorld::kill(TweenHandle handle, bool complete)
{
    if (const auto sequence = m_sequences.find(handle.id); sequence != m_sequences.end())
    {
        for (const std::uint64_t id : sequence->second.running)
        {
            m_tweens.erase(id);
        }
        m_sequences.erase(sequence);
        return;
    }
    const auto found = m_tweens.find(handle.id);
    if (found == m_tweens.end())
    {
        return;
    }
    if (complete)
    {
        // The end it heads to, wherever it is now.
        found->second.completing = true;
        found->second.paused = false;
    }
    else
    {
        m_tweens.erase(found);
    }
}

void TweenWorld::pause(TweenHandle handle)
{
    if (const auto sequence = m_sequences.find(handle.id); sequence != m_sequences.end())
    {
        sequence->second.paused = true;
        for (const std::uint64_t id : sequence->second.running)
        {
            pause(TweenHandle{id});
        }
    }
    else if (const auto found = m_tweens.find(handle.id); found != m_tweens.end())
    {
        found->second.paused = true;
    }
}

void TweenWorld::resume(TweenHandle handle)
{
    if (const auto sequence = m_sequences.find(handle.id); sequence != m_sequences.end())
    {
        sequence->second.paused = false;
        for (const std::uint64_t id : sequence->second.running)
        {
            resume(TweenHandle{id});
        }
    }
    else if (const auto found = m_tweens.find(handle.id); found != m_tweens.end())
    {
        found->second.paused = false;
    }
}

bool TweenWorld::isPlaying(TweenHandle handle) const
{
    return m_tweens.contains(handle.id) || m_sequences.contains(handle.id);
}

void TweenWorld::clear()
{
    m_tweens.clear();
    m_sequences.clear();
    m_tweeners.clear();
}

void TweenWorld::setPaused(bool paused) noexcept
{
    m_paused = paused;
}

std::size_t TweenWorld::activeCount() const noexcept
{
    return m_tweens.size();
}

} // namespace devex::animation
