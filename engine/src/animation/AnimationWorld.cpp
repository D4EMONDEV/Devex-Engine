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

// The properties of a bone that a clip drives.
constexpr std::uint8_t drivesTranslation = 1;
constexpr std::uint8_t drivesRotation = 2;
constexpr std::uint8_t drivesScale = 4;

// How much of each property of a bone a mix of poses holds, from 0 to 1: the weights of the poses
// that drive it.
struct Coverage
{
    float translation = 0.0f;
    float rotation = 0.0f;
    float scale = 0.0f;
};

// Mixes a pose into the poses gathered before it by a weight, times the coverage of each of its
// properties when it has one: each pose keeps its share of what drives a property, and a property
// no pose drives stays out of the mix.
void gather(std::span<JointPose> mix, std::span<Coverage> covered, std::span<const JointPose> pose, float weight,
            std::span<const Coverage> poseCovered = {})
{
    if (weight <= 0.0f)
    {
        return;
    }
    for (std::size_t index = 0; index < mix.size() && index < pose.size(); ++index)
    {
        const JointPose& from = pose[index];
        JointPose& into = mix[index];
        Coverage& total = covered[index];
        const Coverage share = poseCovered.empty() ? Coverage{1.0f, 1.0f, 1.0f} : poseCovered[index];
        if (from.hasTranslation && share.translation > 0.0f)
        {
            const float added = weight * share.translation;
            into.translation = total.translation > 0.0f
                                   ? math::mix(into.translation, from.translation, added / (total.translation + added))
                                   : from.translation;
            total.translation += added;
            into.hasTranslation = true;
        }
        if (from.hasRotation && share.rotation > 0.0f)
        {
            const float added = weight * share.rotation;
            into.rotation = total.rotation > 0.0f
                                ? math::normalize(math::slerp(into.rotation, from.rotation, added / (total.rotation + added)))
                                : from.rotation;
            total.rotation += added;
            into.hasRotation = true;
        }
        if (from.hasScale && share.scale > 0.0f)
        {
            const float added = weight * share.scale;
            into.scale = total.scale > 0.0f ? math::mix(into.scale, from.scale, added / (total.scale + added)) : from.scale;
            total.scale += added;
            into.hasScale = true;
        }
    }
}

// Brings the properties that the clips of a controller drive back towards the rest pose of their
// bones, by as much as the mix leaves them free: a leg that the walk moved stands again in the idle.
void restFill(std::span<JointPose> pose, std::span<const Coverage> covered, std::span<const JointPose> rest,
              std::span<const std::uint8_t> driven)
{
    for (std::size_t index = 0; index < pose.size() && index < rest.size() && index < driven.size(); ++index)
    {
        JointPose& into = pose[index];
        const JointPose& still = rest[index];
        const Coverage& total = covered[index];
        if ((driven[index] & drivesTranslation) != 0 && total.translation < 1.0f)
        {
            into.translation = into.hasTranslation ? math::mix(still.translation, into.translation, total.translation) : still.translation;
            into.hasTranslation = true;
        }
        if ((driven[index] & drivesRotation) != 0 && total.rotation < 1.0f)
        {
            into.rotation = into.hasRotation ? math::normalize(math::slerp(still.rotation, into.rotation, total.rotation)) : still.rotation;
            into.hasRotation = true;
        }
        if ((driven[index] & drivesScale) != 0 && total.scale < 1.0f)
        {
            into.scale = into.hasScale ? math::mix(still.scale, into.scale, total.scale) : still.scale;
            into.hasScale = true;
        }
    }
}

// How far a pose moves from a reference, property by property: what an additive layer adds.
void subtractPose(std::span<JointPose> pose, std::span<const JointPose> reference)
{
    for (std::size_t index = 0; index < pose.size() && index < reference.size(); ++index)
    {
        JointPose& moved = pose[index];
        const JointPose& from = reference[index];
        if (moved.hasTranslation)
        {
            moved.translation = from.hasTranslation ? moved.translation - from.translation : math::Vec3{0.0f};
        }
        if (moved.hasRotation)
        {
            moved.rotation = from.hasRotation ? math::normalize(math::inverse(from.rotation) * moved.rotation) : math::Quat{1.0f, 0.0f, 0.0f, 0.0f};
        }
        if (moved.hasScale)
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                moved.scale[axis] = from.hasScale && std::abs(from.scale[axis]) > 1e-6f ? moved.scale[axis] / from.scale[axis] : 1.0f;
            }
        }
    }
}

// Lays the pose of a layer over the pose below it: on each bone by its mask, the weight of the layer
// and the coverage of the pose, in place of what is below or, additive, added to it. A property the
// pose below leaves alone starts from the rest pose.
void layOver(std::span<JointPose> pose, std::span<const JointPose> layer, std::span<const Coverage> covered,
             std::span<const float> mask, std::span<const JointPose> rest, float weight, bool additive)
{
    const math::Quat identity{1.0f, 0.0f, 0.0f, 0.0f};
    for (std::size_t index = 0; index < pose.size() && index < layer.size() && index < mask.size() && index < rest.size(); ++index)
    {
        const float amount = mask[index] * weight;
        if (amount <= 0.0f)
        {
            continue;
        }
        JointPose& into = pose[index];
        const JointPose& from = layer[index];
        const JointPose& still = rest[index];
        const Coverage& total = covered[index];
        if (const float share = amount * std::min(total.translation, 1.0f); from.hasTranslation && share > 0.0f)
        {
            const math::Vec3 below = into.hasTranslation ? into.translation : still.translation;
            into.translation = additive ? below + from.translation * share : math::mix(below, from.translation, share);
            into.hasTranslation = true;
        }
        if (const float share = amount * std::min(total.rotation, 1.0f); from.hasRotation && share > 0.0f)
        {
            const math::Quat below = into.hasRotation ? into.rotation : still.rotation;
            into.rotation = math::normalize(additive ? below * math::slerp(identity, from.rotation, share) : math::slerp(below, from.rotation, share));
            into.hasRotation = true;
        }
        if (const float share = amount * std::min(total.scale, 1.0f); from.hasScale && share > 0.0f)
        {
            const math::Vec3 below = into.hasScale ? into.scale : still.scale;
            into.scale = additive ? below * math::mix(math::Vec3{1.0f}, from.scale, share) : math::mix(below, from.scale, share);
            into.hasScale = true;
        }
    }
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

    // What a layer of a controller is doing: its state, the one it crossfades from, and the bones of
    // its mask.
    struct LayerRun
    {
        StateRun current;
        StateRun previous;
        float fadeRemaining = 0.0f;
        float fadeLength = 0.0f;
        // The length of the current state in seconds, as last computed.
        float stateDuration = 1.0f;
        // For each bone, 1 in the mask and 0 out of it.
        std::vector<float> mask;
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
        // The clip left behind by the last crossfade of a clip.
        std::shared_ptr<const Clip> previous;
        std::vector<std::int32_t> previousJoints;
        float previousTime = 0.0f;
        float fadeRemaining = 0.0f;
        float fadeLength = 0.0f;

        // The state machine, when the Animator has a controller: a run of each of its layers.
        asset::AssetId controllerId;
        std::shared_ptr<const AnimatorData> controller;
        std::vector<LayerRun> layers;
        // The values of the parameters, set by code or from the controller.
        std::unordered_map<std::string, float> parameters;
        // The weights code gave layers, by name, in place of those of the controller.
        std::unordered_map<std::string, float> layerWeights;
        std::unordered_map<asset::AssetId, ClipBinding> bindings;
        // The properties of each bone that a clip of the controller drives, and the pose the bones had
        // before anything played, kept by entity when the bones are found again.
        std::vector<std::uint8_t> driven;
        std::vector<JointPose> rest;
        std::unordered_map<std::uint64_t, JointPose> rests;
        // The masks of the layers are found again once the bones or the controller change.
        bool masksStale = true;

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
        std::vector<JointPose> layerPose;
        std::vector<JointPose> layered;
        std::vector<JointPose> reference;
        std::vector<Coverage> covered;
        std::vector<Coverage> fadingCovered;
        std::vector<Coverage> layerCovered;
        std::vector<Coverage> layeredCovered;
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
        playback.driven.assign(playback.bones.size(), 0);
        playback.masksStale = true;
        playback.rootBone = -1;
        playback.hasRootTranslation = false;
        // The rest pose of a bone is the one it had when it was first found, before the clips moved it.
        playback.rest.clear();
        for (const Entity bone : playback.bones)
        {
            const auto [found, created] = playback.rests.try_emplace(keyOf(bone));
            if (created)
            {
                const Transform& transform = sceneToRead.get<Transform>(bone);
                found->second = JointPose{.translation = transform.position,
                                          .rotation = transform.rotation,
                                          .scale = transform.scale,
                                          .hasTranslation = true,
                                          .hasRotation = true,
                                          .hasScale = true};
            }
            playback.rest.push_back(found->second);
        }
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

    [[nodiscard]] static std::int32_t stateIndex(const asset::AnimatorLayer& layer, std::string_view name) noexcept
    {
        const auto found = std::ranges::find(layer.states, name, &AnimatorState::name);
        return found != layer.states.end() ? static_cast<std::int32_t>(found - layer.states.begin()) : -1;
    }

    [[nodiscard]] static float valueOf(const Playback& playback, std::string_view name) noexcept
    {
        const auto found = playback.parameters.find(std::string(name));
        return found != playback.parameters.end() ? found->second : 0.0f;
    }

    [[nodiscard]] static const AnimatorState* stateOf(const asset::AnimatorLayer& layer, const StateRun& run) noexcept
    {
        return run.state >= 0 && static_cast<std::size_t>(run.state) < layer.states.size() ? &layer.states[static_cast<std::size_t>(run.state)]
                                                                                           : nullptr;
    }

    // How much of the pose of a layer shows: the weight code gave it, or the controller's, times its
    // weight parameter. The base layer shows whole.
    [[nodiscard]] static float weightOf(const Playback& playback, std::size_t index) noexcept
    {
        if (index == 0)
        {
            return 1.0f;
        }
        const asset::AnimatorLayer& layer = playback.controller->layers[index];
        const auto set = playback.layerWeights.find(layer.name);
        float weight = set != playback.layerWeights.end() ? set->second : layer.weight;
        if (!layer.weightParameter.empty())
        {
            weight *= valueOf(playback, layer.weightParameter);
        }
        return std::clamp(weight, 0.0f, 1.0f);
    }

    // Takes the controller the Animator names, or its new version once the asset changed: the
    // parameters it adds start from their values, and each layer keeps its state when the new
    // version still has both.
    void loadController(Scene& sceneToWrite, Playback& playback, const Animator& animator)
    {
        std::shared_ptr<const AnimatorData> loaded =
            animator.controller.isValid() && animators ? animators(animator.controller) : nullptr;
        const bool sameAsset = playback.controllerId == animator.controller;
        if (loaded == playback.controller && sameAsset)
        {
            return;
        }
        std::unordered_map<std::string, std::string> kept;
        if (sameAsset && playback.controller != nullptr)
        {
            for (std::size_t index = 0; index < playback.layers.size() && index < playback.controller->layers.size(); ++index)
            {
                const asset::AnimatorLayer& layer = playback.controller->layers[index];
                if (const AnimatorState* const state = stateOf(layer, playback.layers[index].current))
                {
                    kept.emplace(layer.name, state->name);
                }
            }
        }
        playback.controllerId = animator.controller;
        playback.controller = std::move(loaded);
        playback.layers.clear();
        playback.bindings.clear();
        std::ranges::fill(playback.driven, std::uint8_t{0});
        playback.masksStale = true;
        if (playback.controller == nullptr)
        {
            return;
        }
        for (const asset::AnimatorParameter& parameter : playback.controller->parameters)
        {
            playback.parameters.try_emplace(parameter.name,
                                            parameter.type == AnimatorParameterType::Trigger ? 0.0f : parameter.defaultValue);
        }
        playback.layers.resize(playback.controller->layers.size());
        for (std::size_t index = 0; index < playback.layers.size(); ++index)
        {
            const asset::AnimatorLayer& layer = playback.controller->layers[index];
            const auto found = kept.find(layer.name);
            const std::int32_t state = found != kept.end() ? stateIndex(layer, found->second) : -1;
            if (state >= 0)
            {
                playback.layers[index].current.state = state;
            }
            else
            {
                enterState(sceneToWrite, playback, index, stateIndex(layer, layer.entry));
            }
        }
    }

    // Starts a state of a layer from its beginning, and the sprite animation it names.
    void enterState(Scene& sceneToWrite, Playback& playback, std::size_t layer, std::int32_t state)
    {
        playback.layers[layer].current = {.state = state};
        if (layer == 0)
        {
            playback.hasRootTranslation = false;
        }
        const AnimatorState* const entered =
            playback.controller != nullptr ? stateOf(playback.controller->layers[layer], playback.layers[layer].current) : nullptr;
        if (entered == nullptr || entered->spriteAnimation.empty())
        {
            return;
        }
        if (scene::SpriteAnimator* const sprite = sceneToWrite.tryGet<scene::SpriteAnimator>(playback.entity))
        {
            if (sprite->animation == entered->spriteAnimation)
            {
                // The same animation again starts over from its first frame.
                sprite->frame = sprite->speed < 0.0f ? std::numeric_limits<std::int32_t>::max() : 0;
                sprite->frameTime = 0.0f;
            }
            sprite->animation = entered->spriteAnimation;
            sprite->playing = true;
        }
    }

    // Whether every condition of a transition is met, and the state far enough for its exit time.
    // A transition without conditions leaves at its exit time, or at the end of the state.
    [[nodiscard]] static bool canLeave(const Playback& playback, const StateRun& run, const AnimatorTransition& transition) noexcept
    {
        const float exitTime = transition.exitTime >= 0.0f ? transition.exitTime : transition.conditions.empty() ? 1.0f : -1.0f;
        if (exitTime >= 0.0f && run.normalized < exitTime)
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

    // Takes the first transition of a layer that may leave its current state, in the order of the
    // controller; the triggers it checks are reset. None is taken during a crossfade.
    void takeTransition(Scene& sceneToWrite, Playback& playback, std::size_t index)
    {
        const asset::AnimatorLayer& layer = playback.controller->layers[index];
        LayerRun& run = playback.layers[index];
        const AnimatorState* const state = stateOf(layer, run.current);
        if (state == nullptr || run.fadeRemaining > 0.0f)
        {
            return;
        }
        for (const AnimatorTransition& transition : layer.transitions)
        {
            const bool fromAny = transition.from.empty();
            if ((!fromAny && transition.from != state->name) || (fromAny && transition.to == state->name) ||
                !canLeave(playback, run.current, transition))
            {
                continue;
            }
            const std::int32_t target = stateIndex(layer, transition.to);
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
            run.previous = transition.duration > 0.0f ? run.current : StateRun{};
            run.fadeLength = transition.duration;
            run.fadeRemaining = transition.duration;
            enterState(sceneToWrite, playback, index, target);
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
            // The properties it drives, as its first frame has them.
            if (found->second.clip != nullptr)
            {
                sampled.assign(found->second.clip->joints().size(), JointPose{});
                found->second.clip->sample(0.0f, sampled);
                for (std::size_t joint = 0; joint < found->second.joints.size() && joint < sampled.size(); ++joint)
                {
                    const std::int32_t bone = found->second.joints[joint];
                    if (bone >= 0 && static_cast<std::size_t>(bone) < playback.driven.size())
                    {
                        std::uint8_t& drives = playback.driven[static_cast<std::size_t>(bone)];
                        drives = static_cast<std::uint8_t>(drives | (sampled[joint].hasTranslation ? drivesTranslation : 0) |
                                                           (sampled[joint].hasRotation ? drivesRotation : 0) |
                                                           (sampled[joint].hasScale ? drivesScale : 0));
                    }
                }
            }
        }
        return found->second.clip != nullptr ? &found->second : nullptr;
    }

    // Binds every clip of the controller at once, so that the bones they drive are known before
    // they play.
    void bindAll(Playback& playback)
    {
        for (const asset::AnimatorLayer& layer : playback.controller->layers)
        {
            for (const AnimatorState& state : layer.states)
            {
                for (const asset::AnimatorMotion& motion : state.motions)
                {
                    static_cast<void>(bindingOf(playback, motion.clip));
                }
            }
        }
    }

    // The mask of each layer on the bones: the closest of a bone and the bones above it that the mask
    // names decides.
    void findMasks(const Scene& sceneToRead, Playback& playback)
    {
        playback.masksStale = false;
        for (std::size_t index = 0; index < playback.layers.size(); ++index)
        {
            const asset::AnimatorLayer& layer = playback.controller->layers[index];
            std::vector<float>& mask = playback.layers[index].mask;
            mask.assign(playback.bones.size(), 1.0f);
            if (layer.mask.empty() && layer.maskExcluded.empty())
            {
                continue;
            }
            for (std::size_t bone = 0; bone < playback.bones.size(); ++bone)
            {
                std::optional<bool> named;
                for (Entity entity = playback.bones[bone]; entity.isValid() && !named; entity = sceneToRead.parent(entity))
                {
                    named = asset::maskNames(layer, sceneToRead.name(entity));
                    if (entity == playback.entity)
                    {
                        break;
                    }
                }
                mask[bone] = named.value_or(layer.mask.empty()) ? 1.0f : 0.0f;
            }
        }
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
    void advanceState(Scene& sceneToRead, Playback& playback, const asset::AnimatorLayer& layer, StateRun& run,
                      const Animator& animator, float delta, float* duration)
    {
        const AnimatorState* const state = stateOf(layer, run);
        if (state == nullptr)
        {
            return;
        }
        const float length = durationOf(sceneToRead, playback, *state);
        if (duration != nullptr)
        {
            *duration = length;
        }
        const float before = run.normalized;
        run.normalized = std::max(0.0f, run.normalized + delta * speedOf(playback, *state, animator) / length);
        if (!state->loop)
        {
            run.normalized = std::min(run.normalized, 1.0f);
        }
        run.wrapped = state->loop && std::floor(run.normalized) != std::floor(before);
    }

    // The pose of a state: its clips at the same place of their cycle, or at their first frame for the
    // reference of an additive layer, mixed by their weights. The coverage says how much of each
    // property its clips drive.
    void statePose(Playback& playback, const asset::AnimatorLayer& layer, const StateRun& run, bool reference,
                   std::vector<JointPose>& pose, std::vector<Coverage>& covered, std::vector<std::int32_t>* rootJoints)
    {
        pose.assign(playback.bones.size(), JointPose{});
        covered.assign(playback.bones.size(), Coverage{});
        const AnimatorState* const state = stateOf(layer, run);
        if (state == nullptr)
        {
            return;
        }
        weightsOf(playback, *state, playback.weights);
        const float cycle = reference ? 0.0f : state->loop ? fraction(run.normalized) : std::min(run.normalized, 1.0f);
        for (std::size_t index = 0; index < state->motions.size(); ++index)
        {
            const float weight = playback.weights[index];
            ClipBinding* const binding = weight > 1e-4f ? bindingOf(playback, state->motions[index].clip) : nullptr;
            if (binding == nullptr)
            {
                continue;
            }
            if (rootJoints != nullptr && rootJoints->empty())
            {
                *rootJoints = binding->joints;
            }
            playback.motion.assign(playback.bones.size(), JointPose{});
            samplePose(*binding->clip, binding->joints, cycle * binding->clip->duration(), playback.motion);
            gather(pose, covered, playback.motion, weight);
        }
    }

    // The pose of a layer and its coverage: its state, crossfaded from the one before, and for an
    // additive layer how far each moves from its first frame.
    void layerPose(Playback& playback, std::size_t index, std::vector<JointPose>& pose, std::vector<Coverage>& covered,
                   std::vector<std::int32_t>* rootJoints)
    {
        const asset::AnimatorLayer& layer = playback.controller->layers[index];
        const LayerRun& run = playback.layers[index];
        const bool additive = index > 0 && layer.blending == asset::AnimatorLayerBlend::Additive;
        statePose(playback, layer, run.current, false, pose, covered, rootJoints);
        if (additive)
        {
            statePose(playback, layer, run.current, true, playback.reference, playback.fadingCovered, nullptr);
            subtractPose(pose, playback.reference);
        }
        if (run.fadeRemaining <= 0.0f || run.fadeLength <= 0.0f || run.previous.state < 0)
        {
            return;
        }
        // The crossfade: each state by its share of the time, so that a state that leaves a property
        // free fades it in or out.
        statePose(playback, layer, run.previous, false, playback.fading, playback.fadingCovered, nullptr);
        if (additive)
        {
            // The coverage of the reference is not needed, and the crossfade below takes its place.
            statePose(playback, layer, run.previous, true, playback.reference, playback.layerCovered, nullptr);
            subtractPose(playback.fading, playback.reference);
        }
        const float progress = 1.0f - run.fadeRemaining / run.fadeLength;
        playback.layerPose.assign(playback.bones.size(), JointPose{});
        playback.layerCovered.assign(playback.bones.size(), Coverage{});
        gather(playback.layerPose, playback.layerCovered, playback.fading, 1.0f - progress, playback.fadingCovered);
        gather(playback.layerPose, playback.layerCovered, pose, progress, covered);
        pose.swap(playback.layerPose);
        covered.swap(playback.layerCovered);
    }

    // Advances one animator that plays a controller, and writes the pose of its bones: the base
    // layer, which brings the bones its states leave free back to rest, then each other layer over it.
    void advanceController(Scene& sceneToWrite, Playback& playback, const Animator& animator, float delta)
    {
        loadController(sceneToWrite, playback, animator);
        if (playback.controller == nullptr || playback.layers.empty())
        {
            return;
        }
        if (bonesAreStale(sceneToWrite, playback))
        {
            bindBones(sceneToWrite, playback);
        }
        if (playback.bindings.empty())
        {
            bindAll(playback);
        }
        if (playback.masksStale)
        {
            findMasks(sceneToWrite, playback);
        }
        const float step = playback.playing && !paused ? std::max(delta, 0.0f) : 0.0f;
        for (std::size_t index = 0; index < playback.layers.size(); ++index)
        {
            const asset::AnimatorLayer& layer = playback.controller->layers[index];
            LayerRun& run = playback.layers[index];
            if (playback.playing && !paused)
            {
                takeTransition(sceneToWrite, playback, index);
            }
            const float before = run.current.normalized;
            advanceState(sceneToWrite, playback, layer, run.current, animator, step, &run.stateDuration);
            if (weightOf(playback, index) > 0.0f)
            {
                passStateEvents(playback, layer, run.current, before);
            }
            if (run.fadeRemaining > 0.0f)
            {
                advanceState(sceneToWrite, playback, layer, run.previous, animator, step, nullptr);
                run.fadeRemaining = std::max(0.0f, run.fadeRemaining - step);
            }
            if (run.fadeRemaining <= 0.0f)
            {
                run.previous = {};
            }
        }

        std::vector<std::int32_t> rootJoints;
        layerPose(playback, 0, playback.pose, playback.covered, &rootJoints);
        restFill(playback.pose, playback.covered, playback.rest, playback.driven);
        applyRootMotion(sceneToWrite, playback, animator, rootJoints, playback.layers.front().current.wrapped, delta);
        for (std::size_t index = 1; index < playback.layers.size(); ++index)
        {
            const float weight = weightOf(playback, index);
            if (weight <= 0.0f)
            {
                continue;
            }
            layerPose(playback, index, playback.layered, playback.layeredCovered, nullptr);
            layOver(playback.pose, playback.layered, playback.layeredCovered, playback.layers[index].mask, playback.rest, weight,
                    playback.controller->layers[index].blending == asset::AnimatorLayerBlend::Additive);
        }
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
    void passStateEvents(Playback& playback, const asset::AnimatorLayer& layer, const StateRun& run, float before)
    {
        const AnimatorState* const state = stateOf(layer, run);
        if (state == nullptr || run.normalized == before)
        {
            return;
        }
        weightsOf(playback, *state, playback.weights);
        const auto heaviest = std::ranges::max_element(playback.weights);
        if (heaviest == playback.weights.end() || *heaviest <= 0.0f)
        {
            return;
        }
        const ClipBinding* const binding =
            bindingOf(playback, state->motions[static_cast<std::size_t>(heaviest - playback.weights.begin())].clip);
        if (binding == nullptr)
        {
            return;
        }
        const float duration = binding->clip->duration();
        passEvents(binding->clip->data(), before * duration, run.normalized * duration, state->loop, !state->loop && run.normalized >= 1.0f,
                   playback.entity);
    }

    // Starts the state machine over: each layer from its entry state.
    void restart(Scene& sceneToWrite, Playback& playback, const Animator& animator)
    {
        loadController(sceneToWrite, playback, animator);
        playback.playing = true;
        if (playback.controller == nullptr)
        {
            return;
        }
        for (std::size_t index = 0; index < playback.layers.size(); ++index)
        {
            const asset::AnimatorLayer& layer = playback.controller->layers[index];
            playback.layers[index] = LayerRun{.mask = std::move(playback.layers[index].mask)};
            enterState(sceneToWrite, playback, index, stateIndex(layer, layer.entry));
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
            playback.layers.clear();
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
    for (std::size_t index = 0; playback->controller != nullptr && index < playback->layers.size(); ++index)
    {
        const asset::AnimatorLayer& layer = playback->controller->layers[index];
        Implementation::LayerRun& run = playback->layers[index];
        run.current = {.state = Implementation::stateIndex(layer, layer.entry)};
        run.previous = {};
        run.fadeRemaining = 0.0f;
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
        return playback->layers.empty() ? 0.0f : std::min(playback->layers.front().current.normalized, 1.0f) * playback->layers.front().stateDuration;
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
        if (!playback->layers.empty())
        {
            Implementation::LayerRun& base = playback->layers.front();
            base.current.normalized = std::max(seconds, 0.0f) / std::max(base.stateDuration, 1e-3f);
        }
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

std::string_view AnimationWorld::state(scene::Entity entity, std::string_view layer) const
{
    const Implementation::Playback* const playback = m_implementation->find(entity);
    const std::optional<std::size_t> index =
        playback != nullptr && playback->controller != nullptr ? playback->controller->findLayer(layer) : std::nullopt;
    if (!index || *index >= playback->layers.size())
    {
        return {};
    }
    const asset::AnimatorState* const state =
        Implementation::stateOf(playback->controller->layers[*index], playback->layers[*index].current);
    return state != nullptr ? std::string_view(state->name) : std::string_view{};
}

float AnimationWorld::stateTime(scene::Entity entity, std::string_view layer) const
{
    const Implementation::Playback* const playback = m_implementation->find(entity);
    const std::optional<std::size_t> index =
        playback != nullptr && playback->controller != nullptr ? playback->controller->findLayer(layer) : std::nullopt;
    return index && *index < playback->layers.size() ? playback->layers[*index].current.normalized : 0.0f;
}

void AnimationWorld::setLayerWeight(scene::Entity entity, std::string_view layer, float weight)
{
    Implementation::Playback& playback = m_implementation->playbackOf(entity);
    if (playback.controller != nullptr && !playback.controller->findLayer(layer))
    {
        m_implementation->warnOnce(std::format("the animator controller has no layer named '{}'", layer));
    }
    playback.layerWeights[std::string(layer)] = std::isfinite(weight) ? std::clamp(weight, 0.0f, 1.0f) : 0.0f;
}

std::optional<float> AnimationWorld::layerWeight(scene::Entity entity, std::string_view layer) const
{
    const Implementation::Playback* const playback = m_implementation->find(entity);
    if (playback == nullptr)
    {
        return std::nullopt;
    }
    if (const auto set = playback->layerWeights.find(std::string(layer)); set != playback->layerWeights.end())
    {
        return set->second;
    }
    const std::optional<std::size_t> index = playback->controller != nullptr ? playback->controller->findLayer(layer) : std::nullopt;
    return index ? std::optional(*index == 0 ? 1.0f : playback->controller->layers[*index].weight) : std::nullopt;
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
    status.playing = playback->playing;
    for (std::size_t index = 0; index < playback->layers.size() && index < controller.layers.size(); ++index)
    {
        const asset::AnimatorLayer& layer = controller.layers[index];
        const Implementation::LayerRun& run = playback->layers[index];
        AnimatorStatus::Layer& shown = status.layers.emplace_back();
        shown.name = layer.name;
        if (const asset::AnimatorState* const state = Implementation::stateOf(layer, run.current))
        {
            shown.state = state->name;
        }
        shown.normalizedTime = run.current.normalized;
        const asset::AnimatorState* const previous = Implementation::stateOf(layer, run.previous);
        if (previous != nullptr && run.fadeRemaining > 0.0f && run.fadeLength > 0.0f)
        {
            shown.previousState = previous->name;
            shown.transitionProgress = 1.0f - run.fadeRemaining / run.fadeLength;
        }
        shown.weight = Implementation::weightOf(*playback, index);
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
