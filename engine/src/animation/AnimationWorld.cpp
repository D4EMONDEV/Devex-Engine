#include <devex/animation/AnimationWorld.hpp>
#include <devex/core/Log.hpp>
#include <devex/scene/AnimationComponents.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/PhysicsComponents.hpp>
#include <devex/scene/Scene.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace devex::animation {
namespace {

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

} // namespace

// What one Animator is doing: the clip that plays, the one that fades out, and the bones both
// drive. Bones are found by name, so the same clip plays on any matching skeleton.
struct AnimationWorld::Implementation
{
    struct Playback
    {
        Entity entity;
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

        std::vector<Entity> bones;
        std::unordered_map<std::string, std::uint32_t> boneIndex;
        // The bone each joint of the clip drives, or -1 when the skeleton has no such bone.
        std::vector<std::int32_t> joints;
        // The topmost bone a clip moves, which root motion takes from the pose.
        std::int32_t rootBone = -1;
        math::Vec3 rootRest{0.0f};
        math::Vec3 previousRootTranslation{0.0f};
        bool hasRootTranslation = false;

        // Scratch space, kept to avoid an allocation every frame.
        std::vector<JointPose> pose;
        std::vector<JointPose> fading;
    };

    ClipSource clips;
    std::unordered_map<std::uint64_t, Playback> playbacks;
    // Scratch space of one sampling, kept to avoid an allocation per animator per frame.
    std::vector<JointPose> sampled;
    const Scene* scene = nullptr;
    bool paused = false;

    [[nodiscard]] Playback* find(Entity entity)
    {
        const auto found = playbacks.find(keyOf(entity));
        return found != playbacks.end() ? &found->second : nullptr;
    }

    [[nodiscard]] std::shared_ptr<const Clip> clipOf(asset::AssetId id)
    {
        return id.isValid() && clips ? clips(id) : nullptr;
    }

    // Finds the bones under the animator again, and maps the joints of its clips onto them.
    void bindBones(const Scene& sceneToRead, Playback& playback)
    {
        playback.bones.clear();
        playback.boneIndex.clear();
        collectBones(sceneToRead, playback.entity, playback.bones, playback.boneIndex);
        playback.joints = mapJoints(playback, playback.clip.get());
        playback.previousJoints = mapJoints(playback, playback.previous.get());
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
    [[nodiscard]] std::int32_t findRootBone(const Scene& sceneToRead, const Playback& playback) const
    {
        for (const std::int32_t bone : playback.joints)
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

    // Advances one animator and writes the pose of its bones.
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
            playback.time += delta * animator.speed;
            if (playback.time >= duration || playback.time < 0.0f)
            {
                if (animator.loop && duration > 0.0f)
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

        applyRootMotion(sceneToWrite, playback, animator, wrapped, delta);
        applyPose(sceneToWrite, playback.bones, playback.pose);
    }

    // Reads the joints of a clip into the bones they drive.
    void samplePose(const Clip& clip, std::span<const std::int32_t> joints, float time,
                    std::vector<JointPose>& pose)
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
    void applyRootMotion(Scene& sceneToWrite, Playback& playback, const Animator& animator, bool wrapped,
                         float delta)
    {
        if (!animator.applyRootMotion)
        {
            playback.hasRootTranslation = false;
            return;
        }
        if (playback.rootBone < 0)
        {
            playback.rootBone = findRootBone(sceneToWrite, playback);
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
};

AnimationWorld::AnimationWorld(ClipSource clips)
    : m_implementation(std::make_unique<Implementation>())
{
    m_implementation->clips = std::move(clips);
}

AnimationWorld::~AnimationWorld() = default;

void AnimationWorld::update(scene::Scene& scene, core::Duration delta)
{
    Implementation& world = *m_implementation;
    if (world.scene != &scene)
    {
        world.playbacks.clear();
        world.scene = &scene;
    }

    std::unordered_set<std::uint64_t> seen;
    seen.reserve(world.playbacks.size());
    for ([[maybe_unused]] auto [entity, animator] : scene.view<Animator>())
    {
        const std::uint64_t key = keyOf(entity);
        seen.insert(key);
        const auto [found, created] = world.playbacks.try_emplace(key);
        Implementation::Playback& playback = found->second;
        if (created)
        {
            playback.entity = entity;
            world.bindBones(scene, playback);
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
}

void AnimationWorld::play(scene::Scene& scene, scene::Entity entity, asset::AssetId clip, float fade)
{
    Animator* const animator = scene.tryGet<Animator>(entity);
    if (animator == nullptr)
    {
        return;
    }
    animator->clip = clip;
    Implementation& world = *m_implementation;
    const auto [found, created] = world.playbacks.try_emplace(keyOf(entity));
    Implementation::Playback& playback = found->second;
    if (created)
    {
        playback.entity = entity;
        world.bindBones(scene, playback);
    }
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
    if (Implementation::Playback* const playback = m_implementation->find(entity))
    {
        playback->playing = false;
        playback->time = 0.0f;
        playback->hasRootTranslation = false;
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
        playback->playing = playback->clip != nullptr;
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
    return playback != nullptr ? playback->time : 0.0f;
}

void AnimationWorld::setTime(scene::Scene& scene, scene::Entity entity, float seconds)
{
    Implementation::Playback* const playback = m_implementation->find(entity);
    const Animator* const animator = scene.tryGet<Animator>(entity);
    if (playback == nullptr || playback->clip == nullptr || animator == nullptr)
    {
        return;
    }
    playback->time = std::clamp(seconds, 0.0f, playback->clip->duration());
    playback->hasRootTranslation = false;
    m_implementation->advance(scene, *playback, *animator, 0.0f);
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

} // namespace devex::animation
