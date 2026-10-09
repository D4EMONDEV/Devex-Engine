#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/AssetId.hpp>
#include <devex/asset/CurveData.hpp>
#include <devex/core/Error.hpp>
#include <devex/core/Time.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/math/Easing.hpp>
#include <devex/math/Math.hpp>
#include <devex/scene/AnimationComponents.hpp>
#include <devex/scene/Entity.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace devex::scene {
class Scene;
struct ComponentType;
} // namespace devex::scene

namespace devex::reflection {
struct FieldInfo;
} // namespace devex::reflection

namespace devex::animation {

// A tween: a field of a component of an entity, from one value to another over some time.
struct DEVEX_API TweenSpec
{
    scene::Entity entity;
    // "Component.field": "Transform.position", "UiRect.opacity", "UiImage.color". The field holds a
    // number, a vector, a color or a rotation. An instance uniform of the shader of a renderer is
    // "SpriteRenderer.instance_shader_parameters/flash", as Godot names it, in four components.
    std::string field;
    // As many components as the field has: x for a number, xyz for a position, all four for a
    // color. A rotation is given in degrees around x, y and z.
    math::Vec4 to{0.0f};
    // The start; nothing starts from the value the field has once the delay is over.
    std::optional<math::Vec4> from;
    // To is added to the start rather than reached.
    bool relative = false;
    // In seconds.
    float duration = 1.0f;
    float delay = 0.0f;
    math::Ease ease = math::Ease::OutQuad;
    // A curve drawn by hand, which replaces the ease when set.
    asset::AssetId curve;
    scene::TweenLoop loop = scene::TweenLoop::None;
    // How many times it plays again once over, with a loop; -1 without end.
    std::int32_t repeats = 0;
};

// A step of a sequence: tweens that play together, then a wait before the next step.
struct DEVEX_API SequenceStep
{
    std::vector<TweenSpec> tweens;
    float interval = 0.0f;
};

struct DEVEX_API TweenHandle
{
    std::uint64_t id = 0;

    [[nodiscard]] bool isValid() const noexcept
    {
        return id != 0;
    }

    bool operator==(const TweenHandle&) const = default;
};

// The tweens of a game that plays: those code starts, alone or in sequences, and those of the
// Tweener components of its scene, which start by themselves. A tween follows its entity by UUID
// and ends with it; the value is written into the component every frame, before the interface is
// laid out and the transforms are computed.
class DEVEX_API TweenWorld
{
public:
    // The curve of an asset, loaded once and shared; null when it cannot be loaded.
    using CurveSource = std::function<std::shared_ptr<const asset::CurveData>(asset::AssetId curve)>;
    // The default value of a uniform of the shader of a material, where a tween of an instance
    // uniform the renderer gives no value starts; nullopt starts it at zero.
    using ShaderDefaults = std::function<std::optional<math::Vec4>(asset::AssetId material, std::string_view uniform)>;

    explicit TweenWorld(CurveSource curves = {}, ShaderDefaults shaderDefaults = {});
    ~TweenWorld();

    TweenWorld(const TweenWorld&) = delete;
    TweenWorld& operator=(const TweenWorld&) = delete;

    // Fails for an entity that does not have the component, or a field it cannot animate.
    [[nodiscard]] core::Result<TweenHandle> play(scene::Scene& scene, const TweenSpec& tween);
    // Plays the steps one after the other; the handle ends with the last one.
    [[nodiscard]] core::Result<TweenHandle> playSequence(scene::Scene& scene, std::vector<SequenceStep> steps);
    // Starts the Tweener of the entity, which does not start by itself without playOnStart.
    [[nodiscard]] core::Result<TweenHandle> playTweener(scene::Scene& scene, scene::Entity entity);

    // Once per frame: Tweeners that appear start, and every tween moves on.
    void update(scene::Scene& scene, core::Duration delta);

    // Stops a tween or a sequence where it is, or, with complete, puts a tween at its end during
    // the next update.
    void kill(TweenHandle handle, bool complete = false);
    void pause(TweenHandle handle);
    void resume(TweenHandle handle);
    // While it plays, waits for its delay or is paused.
    [[nodiscard]] bool isPlaying(TweenHandle handle) const;
    // Forgets every tween, as a new scene does.
    void clear();
    // Holds every tween, as the editor does when the game pauses.
    void setPaused(bool paused) noexcept;
    [[nodiscard]] std::size_t activeCount() const noexcept;

private:
    // Found again every frame by name: game modules and C# register their types again when they
    // reload.
    struct DEVEX_API Target
    {
        const scene::ComponentType* component = nullptr;
        // Null for an instance uniform, which `uniform` names.
        const reflection::FieldInfo* field = nullptr;
        std::string uniform;
    };

    struct DEVEX_API Tween
    {
        core::Uuid entity;
        TweenSpec spec;
        float elapsed = 0.0f;
        std::int32_t repeatsLeft = 0;
        bool forward = true;
        bool started = false;
        bool paused = false;
        // Ended by its entity losing its Tweener.
        bool fromTweener = false;
        // Jumps to its end at the next update, then ends.
        bool completing = false;
        math::Vec4 start{0.0f};
        math::Vec4 end{0.0f};
        math::Quat startRotation{1.0f, 0.0f, 0.0f, 0.0f};
        math::Quat endRotation{1.0f, 0.0f, 0.0f, 0.0f};
    };

    struct DEVEX_API Sequence
    {
        std::vector<SequenceStep> steps;
        std::size_t next = 0;
        std::vector<std::uint64_t> running;
        // The wait after the step that played, or less than zero while its tweens play.
        float waitLeft = -1.0f;
        bool paused = false;
    };

    [[nodiscard]] core::Result<Target> resolve(scene::Scene& scene, scene::Entity entity, std::string_view field) const;
    [[nodiscard]] core::Result<std::uint64_t> start(scene::Scene& scene, const TweenSpec& tween, bool fromTweener);
    // Writes the value at a progress, from 0 at the start to 1 at the end; false once the entity,
    // its component or the field is gone.
    bool write(scene::Scene& scene, Tween& tween, float progress) const;
    bool writeUniform(const Target& target, void* component, Tween& tween, float progress) const;
    // The progress along the ease or the curve of the tween.
    [[nodiscard]] float easedProgress(const Tween& tween, float progress) const;
    // Starts the next step of a sequence; false once it has none.
    bool advance(scene::Scene& scene, Sequence& sequence);

    CurveSource m_curves;
    ShaderDefaults m_shaderDefaults;
    std::unordered_map<std::uint64_t, Tween> m_tweens;
    std::unordered_map<std::uint64_t, Sequence> m_sequences;
    // The tween of each Tweener that started, by entity.
    std::unordered_map<core::Uuid, std::uint64_t> m_tweeners;
    bool m_paused = false;
};

} // namespace devex::animation
