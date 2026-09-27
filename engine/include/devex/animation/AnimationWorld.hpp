#pragma once

#include <devex/animation/Clip.hpp>
#include <devex/asset/AnimatorData.hpp>
#include <devex/asset/AssetId.hpp>
#include <devex/asset/SpriteData.hpp>
#include <devex/core/Time.hpp>
#include <devex/math/Math.hpp>
#include <devex/scene/Entity.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace devex::scene {
class Scene;
}

namespace devex::animation {

// What an animator with a controller is doing, for the tools.
struct AnimatorStatus
{
    struct Parameter
    {
        std::string name;
        asset::AnimatorParameterType type = asset::AnimatorParameterType::Float;
        // Bools and triggers are 0 or 1.
        float value = 0.0f;
    };

    std::string state;
    // Since the state started: 1 at its end, more for a state that loops.
    float normalizedTime = 0.0f;
    // The state it crossfades from, empty when none, and how far the crossfade is, from 0 to 1.
    std::string previousState;
    float transitionProgress = 1.0f;
    bool playing = false;
    std::vector<Parameter> parameters;
};

// The animations of a game that plays: the Animator components of its scene, which pose the bones
// under their entity every frame. Clips drive bones by name, so a clip plays on any skeleton whose
// bones carry the same names. An Animator with a controller plays its state machine: game code sets
// its parameters, and its transitions choose the states, whose clips and blend trees pose the bones.
class AnimationWorld
{
public:
    // The clip of an asset, loaded once and shared; null when it cannot be loaded.
    using ClipSource = std::function<std::shared_ptr<const Clip>(asset::AssetId clip)>;
    // The controller of an asset; null when it cannot be loaded. Asked every frame: a new pointer
    // means the asset changed, and animators that play it take the new one.
    using AnimatorSource = std::function<std::shared_ptr<const asset::AnimatorData>(asset::AssetId controller)>;
    // The sprite frames of a SpriteAnimator, for the length of the sprite animations of states.
    using SpriteFramesSource = std::function<std::shared_ptr<const asset::SpriteFramesData>(asset::AssetId frames)>;

    explicit AnimationWorld(ClipSource clips, AnimatorSource animators = {}, SpriteFramesSource spriteFrames = {});
    ~AnimationWorld();

    AnimationWorld(const AnimationWorld&) = delete;
    AnimationWorld& operator=(const AnimationWorld&) = delete;

    // Once per frame, before the world transforms are updated: animators that appear with
    // playOnStart start, state machines take their transitions, clips advance and pose their bones,
    // and animators that are gone are forgotten. Using the world with another scene starts over
    // from that scene.
    void update(scene::Scene& scene, core::Duration delta);

    // Plays a clip on the animator of the entity, crossfading over fade seconds (its blendTime
    // when fade is negative). The clip becomes the one the Animator holds. An animator with a
    // controller starts its state machine over instead.
    void play(scene::Scene& scene, scene::Entity entity, asset::AssetId clip, float fade = -1.0f);
    // Plays the clip the Animator already holds from its start, or its state machine from its
    // entry state.
    void play(scene::Scene& scene, scene::Entity entity);
    // Stops, back to the start of the clip, or to the entry state.
    void stop(scene::Entity entity);
    void pause(scene::Entity entity);
    void resume(scene::Entity entity);
    [[nodiscard]] bool isPlaying(scene::Entity entity) const;
    // Seconds into the clip, or into the state, and where to continue from.
    [[nodiscard]] float time(scene::Entity entity) const;
    void setTime(scene::Scene& scene, scene::Entity entity, float seconds);

    // The parameters of the state machine of the entity, by name. They may be set before the
    // controller loads; the others start from the values of the controller.
    void setFloat(scene::Entity entity, std::string_view parameter, float value);
    void setInteger(scene::Entity entity, std::string_view parameter, std::int32_t value);
    void setBool(scene::Entity entity, std::string_view parameter, bool value);
    // Sets a trigger, which the transition it lets through resets.
    void setTrigger(scene::Entity entity, std::string_view parameter);
    void resetTrigger(scene::Entity entity, std::string_view parameter);
    // Bools and triggers read as 0 or 1; nothing when the parameter is neither set nor known.
    [[nodiscard]] std::optional<float> parameter(scene::Entity entity, std::string_view parameter) const;
    // The state the machine is in, empty without one; how far into it, 1 at its end.
    [[nodiscard]] std::string_view state(scene::Entity entity) const;
    [[nodiscard]] float stateTime(scene::Entity entity) const;
    [[nodiscard]] std::optional<AnimatorStatus> status(scene::Entity entity) const;

    // Pauses every animator, as the editor does when the game pauses.
    void setPaused(bool paused);
    [[nodiscard]] bool paused() const noexcept;

    // Animators that hold a clip or a controller, playing or paused.
    [[nodiscard]] std::size_t animatorCount() const noexcept;

private:
    struct Implementation;

    std::unique_ptr<Implementation> m_implementation;
};

// Poses the bones under an entity with one clip at one time, without any playback state: what the
// editor shows while it previews a clip. Bones are found by name under the entity.
void applyClip(scene::Scene& scene, scene::Entity entity, const Clip& clip, float time);

// The weights of the clips of a linear blend tree at a value: the two thresholds around the value
// share it, the first and the last take all of it beyond them. Thresholds need not be sorted.
[[nodiscard]] std::vector<float> linearBlendWeights(std::span<const float> thresholds, float value);

// The weights of the clips of a planar blend tree at a point, by gradient band interpolation (the
// "freeform cartesian" blend of Unity): a clip takes all the weight on its position, and the
// weights change smoothly between positions. They add up to 1.
[[nodiscard]] std::vector<float> planarBlendWeights(std::span<const math::Vec2> positions, math::Vec2 value);

} // namespace devex::animation
