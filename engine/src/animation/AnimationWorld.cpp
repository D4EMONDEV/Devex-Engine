#include <devex/animation/AnimationWorld.hpp>
#include <devex/animation/SpriteAnimation.hpp>
#include <devex/core/Log.hpp>
#include <devex/scene/AnimationComponents.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/PhysicsComponents.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/SpriteComponents.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <numeric>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace devex::animation {
namespace {

using asset::AnimatorData;
using asset::AnimatorParameterType;
using asset::AnimatorState;
using asset::AnimatorTest;
using asset::AnimatorTransition;
using scene::Animator;
using scene::Entity;
using scene::Scene;
using scene::Transform;

// Entities keep their handle in a scene, and a key tells a reused index from the entity that had it.
[[nodiscard]] std::uint64_t keyOf(Entity entity) noexcept
{
    return static_cast<std::uint64_t>(entity.generation) << 32 | entity.index;
}

// The entities under the animator, by name: the bones a clip can drive. The animator itself is
// included, so that a clip may drive the root of a model.
void collectBones(const Scene& scene, Entity entity, std::vector<Entity>& bones,
                  std::unordered_map<std::string, std::uint32_t>& byName)
{
    if (scene.tryGet<Transform>(entity) != nullptr)
    {
        const std::string name(scene.name(entity));
        // The first bone of a name wins, as the clip drives one of them.
        byName.try_emplace(name, static_cast<std::uint32_t>(bones.size()));
        bones.push_back(entity);
    }
    for (Entity child = scene.firstChild(entity); child.isValid(); child = scene.nextSibling(child))
    {
        collectBones(scene, child, bones, byName);
    }
}

// Writes a pose into the local transforms of the bones it drives.
void applyPose(Scene& scene, std::span<const Entity> bones, std::span<const JointPose> poses)
{
    for (std::size_t index = 0; index < bones.size() && index < poses.size(); ++index)
    {
        Transform* const transform = scene.tryGet<Transform>(bones[index]);
        const JointPose& pose = poses[index];
        if (transform == nullptr)
        {
            continue;
        }
        if (pose.hasTranslation)
        {
            transform->position = pose.translation;
        }
        if (pose.hasRotation)
        {
            transform->rotation = pose.rotation;
        }
        if (pose.hasScale)
        {
            transform->scale = pose.scale;
        }
    }
}

[[nodiscard]] float fraction(float value) noexcept
{
    return value - std::floor(value);
}

} // namespace

// What one Animator is doing: the clip that plays, or the states of its controller, what fades out,
// and the bones they drive. Bones are found by name, so the same clip plays on any matching skeleton.
struct AnimationWorld::Implementation
{
    // A clip, and the bone each of its joints drives, or -1 when the skeleton has no such bone.
    struct ClipBinding
    {
        std::shared_ptr<const Clip> clip;
        std::vector<std::int32_t> joints;
    };

    // A state of a controller that plays: which one, and how far into it, in cycles.
    struct StateRun
    {
        std::int32_t state = -1;
        float normalized = 0.0f;
        // It started again during this frame.
        bool wrapped = false;
    };

    struct Playback
    {
        Entity entity;
        // Set once the first update has seen its Animator.
        bool started = false;
        asset::AssetId clipId;
        std::shared_ptr<const Clip> clip;
        float time = 0.0f;
        bool playing = false;
        // The clip left behind by the last crossfade.
        std::shared_ptr<const Clip> previous;
        std::vector<std::int32_t> previousJoints;
        float previousTime = 0.0f;
        float fadeRemaining = 0.0f;
        float fadeLength = 0.0f;

        // The state machine, when the Animator has a controller.
        asset::AssetId controllerId;
        std::shared_ptr<const AnimatorData> controller;
        StateRun current;
        StateRun previousState;
        // The length of the current state in seconds, as last computed.
        float stateDuration = 1.0f;
        // The values of the parameters, set by code or from the controller.
        std::unordered_map<std::string, float> parameters;
        std::unordered_map<asset::AssetId, ClipBinding> bindings;

        std::vector<Entity> bones;
        std::unordered_map<std::string, std::uint32_t> boneIndex;
        std::vector<std::int32_t> joints;
        // The topmost bone a clip moves, which root motion takes from the pose.
        std::int32_t rootBone = -1;
        math::Vec3 rootRest{0.0f};
        math::Vec3 previousRootTranslation{0.0f};
        bool hasRootTranslation = false;

        // Scratch space, kept to avoid an allocation every frame.
        std::vector<JointPose> pose;
        std::vector<JointPose> fading;
        std::vector<JointPose> motion;
        std::vector<float> weights;
    };

    ClipSource clips;
    AnimatorSource animators;
    SpriteFramesSource spriteFrames;
    std::unordered_map<std::uint64_t, Playback> playbacks;
    // The events of the last update.
    std::vector<FiredAnimationEvent> events;
    // Scratch space of one sampling, kept to avoid an allocation per animator per frame.
    std::vector<JointPose> sampled;
    const Scene* scene = nullptr;
    bool paused = false;
    std::unordered_set<std::string> warnings;

    [[nodiscard]] Playback* find(Entity entity)
    {
        const auto found = playbacks.find(keyOf(entity));
        return found != playbacks.end() ? &found->second : nullptr;
    }

    [[nodiscard]] const Playback* find(Entity entity) const
    {
        const auto found = playbacks.find(keyOf(entity));
        return found != playbacks.end() ? &found->second : nullptr;
    }

    [[nodiscard]] Playback& playbackOf(Entity entity)
    {
        Playback& playback = playbacks[keyOf(entity)];
        playback.entity = entity;
        return playback;
    }

    [[nodiscard]] std::shared_ptr<const Clip> clipOf(asset::AssetId id)
    {
        return id.isValid() && clips ? clips(id) : nullptr;
    }

    void warnOnce(const std::string& message)
    {
        if (warnings.insert(message).second)
        {
            DEVEX_LOG_WARNING("{}", message);
        }
    }

    // Finds the bones under the animator again, and maps the joints of its clips onto them.
    void bindBones(const Scene& sceneToRead, Playback& playback)
    {
        playback.bones.clear();
        playback.boneIndex.clear();
        collectBones(sceneToRead, playback.entity, playback.bones, playback.boneIndex);
        playback.joints = mapJoints(playback, playback.clip.get());
        playback.previousJoints = mapJoints(playback, playback.previous.get());
        playback.bindings.clear();
        playback.rootBone = -1;
        playback.hasRootTranslation = false;
    }

    [[nodiscard]] static std::vector<std::int32_t> mapJoints(const Playback& playback, const Clip* clip)
    {
        std::vector<std::int32_t> joints;
        if (clip == nullptr)
        {
            return joints;
        }
        for (const std::string& name : clip->joints())
        {
            const auto found = playback.boneIndex.find(name);
            joints.push_back(found != playback.boneIndex.end() ? static_cast<std::int32_t>(found->second) : -1);
        }
        return joints;
    }

    [[nodiscard]] bool bonesAreStale(const Scene& sceneToRead, const Playback& playback) const
    {
        return playback.bones.empty() ||
               std::ranges::any_of(playback.bones, [&](Entity bone) { return !sceneToRead.isAlive(bone); });
    }

    // The bone of the clip that moves the whole skeleton: the first one whose parent is not a bone.
    [[nodiscard]] std::int32_t findRootBone(const Scene& sceneToRead, const Playback& playback,
                                            std::span<const std::int32_t> joints) const
    {
        for (const std::int32_t bone : joints)
        {
            if (bone < 0)
            {
                continue;
            }
            const Entity entity = playback.bones[static_cast<std::size_t>(bone)];
            const Entity parent = sceneToRead.parent(entity);
            const bool parentIsBone =
                parent.isValid() && std::ranges::find(playback.bones, parent) != playback.bones.end() &&
                parent != playback.entity;
            if (!parentIsBone && entity != playback.entity)
            {
                return bone;
            }
        }
        return -1;
    }

    void start(Scene& sceneToRead, Playback& playback, asset::AssetId clip, float fade)
    {
        std::shared_ptr<const Clip> loaded = clipOf(clip);
        if (loaded != nullptr && playback.clip != nullptr && fade > 0.0f)
        {
            playback.previous = playback.clip;
            playback.previousJoints = playback.joints;
            playback.previousTime = playback.time;
            playback.fadeLength = fade;
            playback.fadeRemaining = fade;
        }
        else
        {
            playback.previous.reset();
            playback.previousJoints.clear();
            playback.fadeRemaining = 0.0f;
        }
        playback.clipId = clip;
        playback.clip = std::move(loaded);
        playback.time = 0.0f;
        playback.playing = playback.clip != nullptr;
        playback.hasRootTranslation = false;
        playback.joints = mapJoints(playback, playback.clip.get());
        if (playback.clip != nullptr && bonesAreStale(sceneToRead, playback))
        {
            bindBones(sceneToRead, playback);
        }
    }

    // Advances one animator that plays a clip, and writes the pose of its bones.
    void advance(Scene& sceneToWrite, Playback& playback, const Animator& animator, float delta)
    {
        if (playback.clip == nullptr)
        {
            return;
        }
        if (bonesAreStale(sceneToWrite, playback))
        {
            bindBones(sceneToWrite, playback);
        }
        const float duration = playback.clip->duration();
        bool wrapped = false;
        if (playback.playing && !paused && delta > 0.0f)
        {
            const float before = playback.time;
            playback.time += delta * animator.speed;
            const float reached = playback.time;
            const bool loops = animator.loop && duration > 0.0f;
            if (playback.time >= duration || playback.time < 0.0f)
            {
                if (loops)
                {
                    playback.time -= std::floor(playback.time / duration) * duration;
                    wrapped = true;
                }
                else
                {
                    playback.time = std::clamp(playback.time, 0.0f, duration);
                    playback.playing = false;
                }
            }
            passEvents(playback.clip->data(), before, loops ? reached : playback.time, loops, !loops && !playback.playing,
                       playback.entity);
            if (playback.fadeRemaining > 0.0f)
            {
                playback.fadeRemaining = std::max(0.0f, playback.fadeRemaining - delta);
                playback.previousTime += delta;
            }
        }

        playback.pose.assign(playback.bones.size(), JointPose{});
        samplePose(*playback.clip, playback.joints, playback.time, playback.pose);
        if (playback.previous != nullptr && playback.fadeRemaining > 0.0f && playback.fadeLength > 0.0f)
        {
            playback.fading.assign(playback.bones.size(), JointPose{});
            samplePose(*playback.previous, playback.previousJoints, playback.previousTime, playback.fading);
            const float mixed = 1.0f - playback.fadeRemaining / playback.fadeLength;
            blendPoses(playback.fading, playback.pose, mixed);
            playback.pose.swap(playback.fading);
        }
        else if (playback.previous != nullptr)
        {
            playback.previous.reset();
            playback.previousJoints.clear();
        }

        applyRootMotion(sceneToWrite, playback, animator, playback.joints, wrapped, delta);
        applyPose(sceneToWrite, playback.bones, playback.pose);
    }

    // Reads the joints of a clip into the bones they drive.
    void samplePose(const Clip& clip, std::span<const std::int32_t> joints, float time, std::vector<JointPose>& pose)
    {
        sampled.assign(clip.joints().size(), JointPose{});
        clip.sample(time, sampled);
        for (std::size_t joint = 0; joint < joints.size() && joint < sampled.size(); ++joint)
        {
            if (joints[joint] >= 0)
            {
                pose[static_cast<std::size_t>(joints[joint])] = sampled[joint];
            }
        }
    }

    // Moves the entity with the root bone instead of letting the bone travel away from it.
    void applyRootMotion(Scene& sceneToWrite, Playback& playback, const Animator& animator,
                         std::span<const std::int32_t> joints, bool wrapped, float delta)
    {
        if (!animator.applyRootMotion)
        {
            playback.hasRootTranslation = false;
            return;
        }
        if (playback.rootBone < 0)
        {
            playback.rootBone = findRootBone(sceneToWrite, playback, joints);
            if (playback.rootBone >= 0)
            {
                const Transform* const bone =
                    sceneToWrite.tryGet<Transform>(playback.bones[static_cast<std::size_t>(playback.rootBone)]);
                playback.rootRest = bone != nullptr ? bone->position : math::Vec3{0.0f};
            }
        }
        if (playback.rootBone < 0)
        {
            return;
        }
        JointPose& root = playback.pose[static_cast<std::size_t>(playback.rootBone)];
        if (!root.hasTranslation)
        {
            return;
        }
        const math::Vec3 rootTranslation = root.translation;
        // A clip that looped starts over: its jump back is not motion.
        if (playback.hasRootTranslation && !wrapped)
        {
            const math::Vec3 step = rootTranslation - playback.previousRootTranslation;
            Transform* const transform = sceneToWrite.tryGet<Transform>(playback.entity);
            scene::CharacterController* const character =
                sceneToWrite.tryGet<scene::CharacterController>(playback.entity);
            const math::Vec3 motion = transform != nullptr ? transform->rotation * step : step;
            if (character != nullptr && delta > 0.0f)
            {
                // The character walks the distance of the clip; gravity keeps the vertical speed.
                character->velocity.x = motion.x / delta;
                character->velocity.z = motion.z / delta;
            }
            else if (transform != nullptr)
            {
                transform->position += motion;
            }
        }
        playback.previousRootTranslation = rootTranslation;
        playback.hasRootTranslation = true;
        // The bone itself stays where it was authored, so the mesh does not drift from the entity.
        root.translation = playback.rootRest;
    }

    // ---- State machines ----

    [[nodiscard]] static std::int32_t stateIndex(const AnimatorData& controller, std::string_view name) noexcept
    {
        const auto found = std::ranges::find(controller.states, name, &AnimatorState::name);
        return found != controller.states.end() ? static_cast<std::int32_t>(found - controller.states.begin()) : -1;
    }

    [[nodiscard]] static float valueOf(const Playback& playback, std::string_view name) noexcept
    {
        const auto found = playback.parameters.find(std::string(name));
        return found != playback.parameters.end() ? found->second : 0.0f;
    }

    // Takes the controller the Animator names, or its new version once the asset changed: the
    // parameters it adds start from their values, and the machine keeps its state when the new
    // version still has it.
    void loadController(Scene& sceneToWrite, Playback& playback, const Animator& animator)
    {
        std::shared_ptr<const AnimatorData> loaded =
            animator.controller.isValid() && animators ? animators(animator.controller) : nullptr;
        const bool sameAsset = playback.controllerId == animator.controller;
        if (loaded == playback.controller && sameAsset)
        {
            return;
        }
        const std::string kept =
            sameAsset && playback.controller != nullptr && playback.current.state >= 0 &&
                    static_cast<std::size_t>(playback.current.state) < playback.controller->states.size()
                ? playback.controller->states[static_cast<std::size_t>(playback.current.state)].name
                : std::string{};
        playback.controllerId = animator.controller;
        playback.controller = std::move(loaded);
        playback.previousState = {};
        playback.fadeRemaining = 0.0f;
        playback.bindings.clear();
        if (playback.controller == nullptr)
        {
            playback.current = {};
            return;
        }
        for (const asset::AnimatorParameter& parameter : playback.controller->parameters)
        {
            playback.parameters.try_emplace(parameter.name,
                                            parameter.type == AnimatorParameterType::Trigger ? 0.0f : parameter.defaultValue);
        }
        const std::int32_t state = stateIndex(*playback.controller, kept);
        if (state >= 0)
        {
            playback.current.state = state;
        }
        else
        {
            enterState(sceneToWrite, playback, stateIndex(*playback.controller, playback.controller->entry));
        }
    }

    // Starts a state from its beginning, and the sprite animation it names.
    void enterState(Scene& sceneToWrite, Playback& playback, std::int32_t state)
    {
        playback.current = {.state = state};
        playback.hasRootTranslation = false;
        if (playback.controller == nullptr || state < 0)
        {
            return;
        }
        const AnimatorState& entered = playback.controller->states[static_cast<std::size_t>(state)];
        if (entered.spriteAnimation.empty())
        {
            return;
        }
        if (scene::SpriteAnimator* const sprite = sceneToWrite.tryGet<scene::SpriteAnimator>(playback.entity))
        {
            if (sprite->animation == entered.spriteAnimation)
            {
                // The same animation again starts over from its first frame.
                sprite->frame = sprite->speed < 0.0f ? std::numeric_limits<std::int32_t>::max() : 0;
                sprite->frameTime = 0.0f;
            }
            sprite->animation = entered.spriteAnimation;
            sprite->playing = true;
        }
    }

    // Whether every condition of a transition is met, and the state far enough for its exit time.
    // A transition without conditions leaves at its exit time, or at the end of the state.
    [[nodiscard]] static bool canLeave(const Playback& playback, const AnimatorTransition& transition) noexcept
    {
        const float exitTime = transition.exitTime >= 0.0f ? transition.exitTime : transition.conditions.empty() ? 1.0f : -1.0f;
        if (exitTime >= 0.0f && playback.current.normalized < exitTime)
        {
            return false;
        }
        for (const asset::AnimatorCondition& condition : transition.conditions)
        {
            const float value = valueOf(playback, condition.parameter);
            bool met = false;
            switch (condition.test)
            {
            case AnimatorTest::Greater:
                met = value > condition.value;
                break;
            case AnimatorTest::Less:
                met = value < condition.value;
                break;
            case AnimatorTest::Equals:
                met = std::lround(value) == std::lround(condition.value);
                break;
            case AnimatorTest::NotEquals:
                met = std::lround(value) != std::lround(condition.value);
                break;
            case AnimatorTest::IsTrue:
            case AnimatorTest::Triggered:
                met = value != 0.0f;
                break;
            case AnimatorTest::IsFalse:
                met = value == 0.0f;
                break;
            }
            if (!met)
            {
                return false;
            }
        }
        return true;
    }

    // Takes the first transition that may leave the current state, in the order of the controller;
    // the triggers it checks are reset. None is taken during a crossfade.
    void takeTransition(Scene& sceneToWrite, Playback& playback)
    {
        const AnimatorData& controller = *playback.controller;
        if (playback.current.state < 0 || playback.fadeRemaining > 0.0f)
        {
            return;
        }
        const std::string& current = controller.states[static_cast<std::size_t>(playback.current.state)].name;
        for (const AnimatorTransition& transition : controller.transitions)
        {
            const bool fromAny = transition.from.empty();
            if ((!fromAny && transition.from != current) || (fromAny && transition.to == current) ||
                !canLeave(playback, transition))
            {
                continue;
            }
            const std::int32_t target = stateIndex(controller, transition.to);
            if (target < 0)
            {
                continue;
            }
            for (const asset::AnimatorCondition& condition : transition.conditions)
            {
                if (condition.test == AnimatorTest::Triggered)
                {
                    playback.parameters[condition.parameter] = 0.0f;
                }
            }
            playback.previousState = playback.current;
            playback.fadeLength = transition.duration;
            playback.fadeRemaining = transition.duration;
            if (transition.duration <= 0.0f)
            {
                playback.previousState = {};
            }
            enterState(sceneToWrite, playback, target);
            return;
        }
    }

    [[nodiscard]] ClipBinding* bindingOf(Playback& playback, asset::AssetId clip)
    {
        const auto [found, created] = playback.bindings.try_emplace(clip);
        if (created)
        {
            found->second.clip = clipOf(clip);
            found->second.joints = mapJoints(playback, found->second.clip.get());
        }
        return found->second.clip != nullptr ? &found->second : nullptr;
    }

    // The weight of each clip of a state, from its blend and its parameters.
    void weightsOf(const Playback& playback, const AnimatorState& state, std::vector<float>& weights) const
    {
        weights.assign(state.motions.size(), 0.0f);
        if (state.motions.empty())
        {
            return;
        }
        switch (state.blend)
        {
        case asset::AnimatorBlend::None:
            weights.front() = 1.0f;
            break;
        case asset::AnimatorBlend::Linear: {
            std::vector<float> thresholds;
            for (const asset::AnimatorMotion& motion : state.motions)
            {
                thresholds.push_back(motion.threshold);
            }
            weights = linearBlendWeights(thresholds, valueOf(playback, state.parameter));
            break;
        }
        case asset::AnimatorBlend::Planar: {
            std::vector<math::Vec2> positions;
            for (const asset::AnimatorMotion& motion : state.motions)
            {
                positions.push_back(motion.position);
            }
            weights = planarBlendWeights(positions, {valueOf(playback, state.parameter), valueOf(playback, state.parameterY)});
            break;
        }
        }
    }

    // How long a state lasts in seconds: its clips, weighted as they blend, or its sprite animation.
    [[nodiscard]] float durationOf(Scene& sceneToRead, Playback& playback, const AnimatorState& state)
    {
        weightsOf(playback, state, playback.weights);
        float duration = 0.0f;
        float total = 0.0f;
        for (std::size_t index = 0; index < state.motions.size(); ++index)
        {
            const ClipBinding* const binding = playback.weights[index] > 0.0f ? bindingOf(playback, state.motions[index].clip) : nullptr;
            if (binding != nullptr)
            {
                duration += binding->clip->duration() * playback.weights[index];
                total += playback.weights[index];
            }
        }
        if (total > 0.0f)
        {
            return std::max(duration / total, 1e-3f);
        }
        if (!state.spriteAnimation.empty() && spriteFrames)
        {
            const scene::SpriteAnimator* const sprite = sceneToRead.tryGet<scene::SpriteAnimator>(playback.entity);
            const std::shared_ptr<const asset::SpriteFramesData> frames =
                sprite != nullptr && sprite->frames.isValid() ? spriteFrames(sprite->frames) : nullptr;
            const auto animation =
                frames != nullptr ? std::ranges::find(frames->animations, state.spriteAnimation, &asset::SpriteAnimationData::name)
                                  : std::vector<asset::SpriteAnimationData>::const_iterator{};
            if (frames != nullptr && animation != frames->animations.end() && animation->fps > 0.0f && !animation->frames.empty())
            {
                return static_cast<float>(animation->frames.size()) / animation->fps;
            }
        }
        return 1.0f;
    }

    [[nodiscard]] float speedOf(const Playback& playback, const AnimatorState& state, const Animator& animator) const
    {
        const float multiplier = state.speedParameter.empty() ? 1.0f : valueOf(playback, state.speedParameter);
        return state.speed * multiplier * animator.speed;
    }

    // Moves a state on by a duration, in cycles of its length.
    void advanceState(Scene& sceneToRead, Playback& playback, StateRun& run, const Animator& animator, float delta,
                      float* duration)
    {
        if (run.state < 0)
        {
            return;
        }
        const AnimatorState& state = playback.controller->states[static_cast<std::size_t>(run.state)];
        const float length = durationOf(sceneToRead, playback, state);
        if (duration != nullptr)
        {
            *duration = length;
        }
        const float before = run.normalized;
        run.normalized = std::max(0.0f, run.normalized + delta * speedOf(playback, state, animator) / length);
        if (!state.loop)
        {
            run.normalized = std::min(run.normalized, 1.0f);
        }
        run.wrapped = state.loop && std::floor(run.normalized) != std::floor(before);
    }

    // The pose of a state: its clips at the same place of their cycle, mixed by their weights.
    void statePose(Playback& playback, const StateRun& run, std::vector<JointPose>& pose,
                   std::vector<std::int32_t>* rootJoints)
    {
        pose.assign(playback.bones.size(), JointPose{});
        if (run.state < 0)
        {
            return;
        }
        const AnimatorState& state = playback.controller->states[static_cast<std::size_t>(run.state)];
        weightsOf(playback, state, playback.weights);
        const float cycle = state.loop ? fraction(run.normalized) : std::min(run.normalized, 1.0f);
        float total = 0.0f;
        for (std::size_t index = 0; index < state.motions.size(); ++index)
        {
            const float weight = playback.weights[index];
            ClipBinding* const binding = weight > 1e-4f ? bindingOf(playback, state.motions[index].clip) : nullptr;
            if (binding == nullptr)
            {
                continue;
            }
            if (rootJoints != nullptr && rootJoints->empty())
            {
                *rootJoints = binding->joints;
            }
            if (total == 0.0f)
            {
                samplePose(*binding->clip, binding->joints, cycle * binding->clip->duration(), pose);
                total = weight;
                continue;
            }
            playback.motion.assign(playback.bones.size(), JointPose{});
            samplePose(*binding->clip, binding->joints, cycle * binding->clip->duration(), playback.motion);
            total += weight;
            blendPoses(pose, playback.motion, weight / total);
        }
    }

    // Advances one animator that plays a controller, and writes the pose of its bones.
    void advanceController(Scene& sceneToWrite, Playback& playback, const Animator& animator, float delta)
    {
        loadController(sceneToWrite, playback, animator);
        if (playback.controller == nullptr || playback.current.state < 0)
        {
            return;
        }
        if (bonesAreStale(sceneToWrite, playback))
        {
            bindBones(sceneToWrite, playback);
        }
        const float step = playback.playing && !paused ? std::max(delta, 0.0f) : 0.0f;
        if (playback.playing && !paused)
        {
            takeTransition(sceneToWrite, playback);
        }
        const float before = playback.current.normalized;
        advanceState(sceneToWrite, playback, playback.current, animator, step, &playback.stateDuration);
        passStateEvents(playback, playback.current, before);
        if (playback.fadeRemaining > 0.0f)
        {
            advanceState(sceneToWrite, playback, playback.previousState, animator, step, nullptr);
            playback.fadeRemaining = std::max(0.0f, playback.fadeRemaining - step);
        }

        std::vector<std::int32_t> rootJoints;
        statePose(playback, playback.current, playback.pose, &rootJoints);
        if (playback.fadeRemaining > 0.0f && playback.fadeLength > 0.0f && playback.previousState.state >= 0)
        {
            statePose(playback, playback.previousState, playback.fading, nullptr);
            blendPoses(playback.fading, playback.pose, 1.0f - playback.fadeRemaining / playback.fadeLength);
            playback.pose.swap(playback.fading);
        }
        else
        {
            playback.previousState = {};
        }
        applyRootMotion(sceneToWrite, playback, animator, rootJoints, playback.current.wrapped, delta);
        applyPose(sceneToWrite, playback.bones, playback.pose);
    }

    // The events of a clip between two of its times, as its playback goes: from `from`, included, to
    // `to`, left out, forward, and the other way backward. Around a clip that loops, `to` goes past
    // its end or before its start, and a long step passes a few loops at most; a clip that does not
    // loop passes the events at the end it reaches.
    void passEvents(const asset::AnimationClipData& clip, float from, float to, bool loop, bool reachedEnd, Entity entity)
    {
        if (clip.events.empty() || from == to)
        {
            return;
        }
        const float duration = std::max(clip.duration, 1e-6f);
        const bool forward = to > from;
        if (!loop)
        {
            for (std::size_t index = 0; index < clip.events.size(); ++index)
            {
                const asset::AnimationEvent& event = clip.events[forward ? index : clip.events.size() - 1 - index];
                const bool passed = forward ? event.time >= from && (event.time < to || (reachedEnd && event.time <= to))
                                            : event.time <= from && (event.time > to || (reachedEnd && event.time >= to));
                if (passed)
                {
                    events.push_back({entity, event.name});
                }
            }
            return;
        }
        constexpr float maxLoops = 4.0f;
        const float low = std::max(std::min(from, to), std::max(from, to) - duration * maxLoops);
        const float high = std::max(from, to);
        const auto first = static_cast<std::int64_t>(std::floor(low / duration));
        const auto last = static_cast<std::int64_t>(std::floor(high / duration));
        for (std::int64_t cycle = forward ? first : last; forward ? cycle <= last : cycle >= first; cycle += forward ? 1 : -1)
        {
            for (std::size_t index = 0; index < clip.events.size(); ++index)
            {
                const asset::AnimationEvent& event = clip.events[forward ? index : clip.events.size() - 1 - index];
                const float at = static_cast<float>(cycle) * duration + event.time;
                if (forward ? at >= low && at < high : at > low && at <= high)
                {
                    events.push_back({entity, event.name});
                }
            }
        }
    }

    // The events of the clip that weighs the most in a state, as it moved on from `before`, in cycles.
    void passStateEvents(Playback& playback, const StateRun& run, float before)
    {
        if (run.state < 0 || run.normalized == before)
        {
            return;
        }
        const AnimatorState& state = playback.controller->states[static_cast<std::size_t>(run.state)];
        weightsOf(playback, state, playback.weights);
        const auto heaviest = std::ranges::max_element(playback.weights);
        if (heaviest == playback.weights.end() || *heaviest <= 0.0f)
        {
            return;
        }
        const ClipBinding* const binding =
            bindingOf(playback, state.motions[static_cast<std::size_t>(heaviest - playback.weights.begin())].clip);
        if (binding == nullptr)
        {
            return;
        }
        const float duration = binding->clip->duration();
        passEvents(binding->clip->data(), before * duration, run.normalized * duration, state.loop, !state.loop && run.normalized >= 1.0f,
                   playback.entity);
    }

    // Starts the state machine over from its entry state.
    void restart(Scene& sceneToWrite, Playback& playback, const Animator& animator)
    {
        loadController(sceneToWrite, playback, animator);
        playback.previousState = {};
        playback.fadeRemaining = 0.0f;
        playback.playing = true;
        if (playback.controller != nullptr)
        {
            enterState(sceneToWrite, playback, stateIndex(*playback.controller, playback.controller->entry));
        }
    }

    void setParameter(Entity entity, std::string_view name, float value)
    {
        Playback& playback = playbackOf(entity);
        if (playback.controller != nullptr && playback.controller->findParameter(name) == nullptr)
        {
            warnOnce(std::format("the animator controller has no parameter named '{}'", name));
        }
        playback.parameters[std::string(name)] = value;
    }
};

AnimationWorld::AnimationWorld(ClipSource clips, AnimatorSource animators, SpriteFramesSource spriteFrames)
    : m_implementation(std::make_unique<Implementation>())
{
    m_implementation->clips = std::move(clips);
    m_implementation->animators = std::move(animators);
    m_implementation->spriteFrames = std::move(spriteFrames);
}

AnimationWorld::~AnimationWorld() = default;

void AnimationWorld::update(scene::Scene& scene, core::Duration delta)
{
    Implementation& world = *m_implementation;
    if (world.scene != &scene)
    {
        // Parameters set before the first update are the scene's; another scene starts over.
        if (world.scene != nullptr)
        {
            world.playbacks.clear();
        }
        world.scene = &scene;
    }

    world.events.clear();
    std::unordered_set<std::uint64_t> seen;
    seen.reserve(world.playbacks.size());
    for ([[maybe_unused]] auto [entity, animator] : scene.view<Animator>())
    {
        const std::uint64_t key = keyOf(entity);
        seen.insert(key);
        Implementation::Playback& playback = world.playbacks[key];
        const bool created = !playback.started;
        if (created)
        {
            playback.entity = entity;
            playback.started = true;
            world.bindBones(scene, playback);
        }
        if (animator.controller.isValid())
        {
            if (created)
            {
                world.restart(scene, playback, animator);
                playback.playing = animator.playOnStart;
            }
            world.advanceController(scene, playback, animator, static_cast<float>(delta.count()));
            continue;
        }
        if (playback.controller != nullptr || playback.controllerId.isValid())
        {
            // The controller was taken away: the Animator plays its clip again.
            playback.controller.reset();
            playback.controllerId = {};
            playback.clipId = {};
        }
        if (created)
        {
            if (animator.playOnStart)
            {
                world.start(scene, playback, animator.clip, 0.0f);
            }
            else
            {
                playback.clipId = animator.clip;
                playback.clip = world.clipOf(animator.clip);
                playback.joints = Implementation::mapJoints(playback, playback.clip.get());
            }
        }
        else if (playback.clipId != animator.clip)
        {
            // The clip was changed from the inspector or from game code.
            world.start(scene, playback, animator.clip, std::max(animator.blendTime, 0.0f));
        }
        world.advance(scene, playback, animator, static_cast<float>(delta.count()));
    }

    std::erase_if(world.playbacks, [&seen](const auto& entry) { return !seen.contains(entry.first); });

    // Sprites move from frame to frame while the game plays.
    if (!world.paused && world.spriteFrames)
    {
        updateSpriteAnimators(scene, world.spriteFrames, static_cast<float>(delta.count()), &world.events);
    }
}

std::span<const FiredAnimationEvent> AnimationWorld::events() const noexcept
{
    return m_implementation->events;
}

void AnimationWorld::play(scene::Scene& scene, scene::Entity entity, asset::AssetId clip, float fade)
{
    Animator* const animator = scene.tryGet<Animator>(entity);
    if (animator == nullptr)
    {
        return;
    }
    Implementation& world = *m_implementation;
    Implementation::Playback& playback = world.playbackOf(entity);
    if (!playback.started)
    {
        playback.started = true;
        world.bindBones(scene, playback);
    }
    if (animator->controller.isValid())
    {
        if (clip.isValid() && clip != animator->clip)
        {
            world.warnOnce(std::format("the Animator of '{}' plays its controller, not a clip", scene.name(entity)));
        }
        world.restart(scene, playback, *animator);
        return;
    }
    animator->clip = clip;
    world.start(scene, playback, clip, fade < 0.0f ? std::max(animator->blendTime, 0.0f) : fade);
}

void AnimationWorld::play(scene::Scene& scene, scene::Entity entity)
{
    const Animator* const animator = scene.tryGet<Animator>(entity);
    if (animator != nullptr)
    {
        play(scene, entity, animator->clip, 0.0f);
    }
}

void AnimationWorld::stop(scene::Entity entity)
{
    Implementation::Playback* const playback = m_implementation->find(entity);
    if (playback == nullptr)
    {
        return;
    }
    playback->playing = false;
    playback->time = 0.0f;
    playback->hasRootTranslation = false;
    if (playback->controller != nullptr)
    {
        playback->current = {.state = Implementation::stateIndex(*playback->controller, playback->controller->entry)};
        playback->previousState = {};
        playback->fadeRemaining = 0.0f;
    }
}

void AnimationWorld::pause(scene::Entity entity)
{
    if (Implementation::Playback* const playback = m_implementation->find(entity))
    {
        playback->playing = false;
    }
}

void AnimationWorld::resume(scene::Entity entity)
{
    if (Implementation::Playback* const playback = m_implementation->find(entity))
    {
        playback->playing = playback->clip != nullptr || playback->controller != nullptr;
    }
}

bool AnimationWorld::isPlaying(scene::Entity entity) const
{
    const Implementation::Playback* const playback = m_implementation->find(entity);
    return playback != nullptr && playback->playing;
}

float AnimationWorld::time(scene::Entity entity) const
{
    const Implementation::Playback* const playback = m_implementation->find(entity);
    if (playback == nullptr)
    {
        return 0.0f;
    }
    if (playback->controller != nullptr)
    {
        return std::min(playback->current.normalized, 1.0f) * playback->stateDuration;
    }
    return playback->time;
}

void AnimationWorld::setTime(scene::Scene& scene, scene::Entity entity, float seconds)
{
    Implementation::Playback* const playback = m_implementation->find(entity);
    const Animator* const animator = scene.tryGet<Animator>(entity);
    if (playback == nullptr || animator == nullptr)
    {
        return;
    }
    playback->hasRootTranslation = false;
    if (playback->controller != nullptr)
    {
        playback->current.normalized = std::max(seconds, 0.0f) / std::max(playback->stateDuration, 1e-3f);
        m_implementation->advanceController(scene, *playback, *animator, 0.0f);
        return;
    }
    if (playback->clip == nullptr)
    {
        return;
    }
    playback->time = std::clamp(seconds, 0.0f, playback->clip->duration());
    m_implementation->advance(scene, *playback, *animator, 0.0f);
}

void AnimationWorld::setFloat(scene::Entity entity, std::string_view parameter, float value)
{
    m_implementation->setParameter(entity, parameter, value);
}

void AnimationWorld::setInteger(scene::Entity entity, std::string_view parameter, std::int32_t value)
{
    m_implementation->setParameter(entity, parameter, static_cast<float>(value));
}

void AnimationWorld::setBool(scene::Entity entity, std::string_view parameter, bool value)
{
    m_implementation->setParameter(entity, parameter, value ? 1.0f : 0.0f);
}

void AnimationWorld::setTrigger(scene::Entity entity, std::string_view parameter)
{
    m_implementation->setParameter(entity, parameter, 1.0f);
}

void AnimationWorld::resetTrigger(scene::Entity entity, std::string_view parameter)
{
    m_implementation->setParameter(entity, parameter, 0.0f);
}

std::optional<float> AnimationWorld::parameter(scene::Entity entity, std::string_view parameter) const
{
    const Implementation::Playback* const playback = m_implementation->find(entity);
    if (playback == nullptr)
    {
        return std::nullopt;
    }
    const auto found = playback->parameters.find(std::string(parameter));
    return found != playback->parameters.end() ? std::optional(found->second) : std::nullopt;
}

std::string_view AnimationWorld::state(scene::Entity entity) const
{
    const Implementation::Playback* const playback = m_implementation->find(entity);
    if (playback == nullptr || playback->controller == nullptr || playback->current.state < 0)
    {
        return {};
    }
    return playback->controller->states[static_cast<std::size_t>(playback->current.state)].name;
}

float AnimationWorld::stateTime(scene::Entity entity) const
{
    const Implementation::Playback* const playback = m_implementation->find(entity);
    return playback != nullptr && playback->controller != nullptr ? playback->current.normalized : 0.0f;
}

std::optional<AnimatorStatus> AnimationWorld::status(scene::Entity entity) const
{
    const Implementation::Playback* const playback = m_implementation->find(entity);
    if (playback == nullptr || playback->controller == nullptr)
    {
        return std::nullopt;
    }
    const AnimatorData& controller = *playback->controller;
    AnimatorStatus status;
    status.state = std::string(state(entity));
    status.normalizedTime = playback->current.normalized;
    status.playing = playback->playing;
    if (playback->previousState.state >= 0 && playback->fadeRemaining > 0.0f && playback->fadeLength > 0.0f)
    {
        status.previousState = controller.states[static_cast<std::size_t>(playback->previousState.state)].name;
        status.transitionProgress = 1.0f - playback->fadeRemaining / playback->fadeLength;
    }
    for (const asset::AnimatorParameter& parameter : controller.parameters)
    {
        status.parameters.push_back({parameter.name, parameter.type, Implementation::valueOf(*playback, parameter.name)});
    }
    return status;
}

void AnimationWorld::setPaused(bool paused)
{
    m_implementation->paused = paused;
}

bool AnimationWorld::paused() const noexcept
{
    return m_implementation->paused;
}

std::size_t AnimationWorld::animatorCount() const noexcept
{
    return m_implementation->playbacks.size();
}

void applyClip(scene::Scene& scene, scene::Entity entity, const Clip& clip, float time)
{
    std::vector<Entity> bones;
    std::unordered_map<std::string, std::uint32_t> byName;
    collectBones(scene, entity, bones, byName);
    std::vector<JointPose> sampled(clip.joints().size());
    clip.sample(time, sampled);

    std::vector<JointPose> pose(bones.size());
    for (std::size_t joint = 0; joint < clip.joints().size(); ++joint)
    {
        const auto found = byName.find(clip.joints()[joint]);
        if (found != byName.end())
        {
            pose[found->second] = sampled[joint];
        }
    }
    applyPose(scene, bones, pose);
}

std::vector<float> linearBlendWeights(std::span<const float> thresholds, float value)
{
    std::vector<float> weights(thresholds.size(), 0.0f);
    if (thresholds.empty())
    {
        return weights;
    }
    std::vector<std::size_t> order(thresholds.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::stable_sort(order, {}, [&](std::size_t index) { return thresholds[index]; });
    if (value <= thresholds[order.front()])
    {
        weights[order.front()] = 1.0f;
        return weights;
    }
    if (value >= thresholds[order.back()])
    {
        weights[order.back()] = 1.0f;
        return weights;
    }
    for (std::size_t index = 0; index + 1 < order.size(); ++index)
    {
        const float low = thresholds[order[index]];
        const float high = thresholds[order[index + 1]];
        if (value >= low && value <= high)
        {
            const float mix = high > low ? (value - low) / (high - low) : 0.0f;
            weights[order[index]] = 1.0f - mix;
            weights[order[index + 1]] = mix;
            break;
        }
    }
    return weights;
}

std::vector<float> planarBlendWeights(std::span<const math::Vec2> positions, math::Vec2 value)
{
    std::vector<float> weights(positions.size(), 0.0f);
    if (positions.empty())
    {
        return weights;
    }
    float total = 0.0f;
    for (std::size_t index = 0; index < positions.size(); ++index)
    {
        float weight = 1.0f;
        for (std::size_t other = 0; other < positions.size(); ++other)
        {
            const math::Vec2 between = positions[other] - positions[index];
            const float length = math::dot(between, between);
            if (other == index || length <= 0.0f)
            {
                continue;
            }
            weight = std::min(weight, 1.0f - math::dot(value - positions[index], between) / length);
        }
        weights[index] = std::max(weight, 0.0f);
        total += weights[index];
    }
    if (total > 0.0f)
    {
        for (float& weight : weights)
        {
            weight /= total;
        }
        return weights;
    }
    // Outside every band: the closest clip.
    const auto closest = std::ranges::min_element(positions, {}, [&](math::Vec2 position) { return math::length(position - value); });
    weights[static_cast<std::size_t>(closest - positions.begin())] = 1.0f;
    return weights;
}

} // namespace devex::animation
