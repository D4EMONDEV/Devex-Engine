#pragma once

#include <devex/animation/Clip.hpp>
#include <devex/asset/AssetId.hpp>
#include <devex/core/Time.hpp>
#include <devex/scene/Entity.hpp>

#include <cstddef>
#include <functional>
#include <memory>

namespace devex::scene {
class Scene;
}

namespace devex::animation {

// The animations of a game that plays: the Animator components of its scene, which pose the bones
// under their entity every frame. Clips drive bones by name, so a clip plays on any skeleton whose
// bones carry the same names.
class AnimationWorld
{
public:
    // The clip of an asset, loaded once and shared; null when it cannot be loaded.
    using ClipSource = std::function<std::shared_ptr<const Clip>(asset::AssetId clip)>;

    explicit AnimationWorld(ClipSource clips);
    ~AnimationWorld();

    AnimationWorld(const AnimationWorld&) = delete;
    AnimationWorld& operator=(const AnimationWorld&) = delete;

    // Once per frame, before the world transforms are updated: animators that appear with
    // playOnStart start, clips advance and pose their bones, and animators that are gone are
    // forgotten. Using the world with another scene starts over from that scene.
    void update(scene::Scene& scene, core::Duration delta);

    // Plays a clip on the animator of the entity, crossfading over fade seconds (its blendTime
    // when fade is negative). The clip becomes the one the Animator holds.
    void play(scene::Scene& scene, scene::Entity entity, asset::AssetId clip, float fade = -1.0f);
    // Plays the clip the Animator already holds, from its start.
    void play(scene::Scene& scene, scene::Entity entity);
    void stop(scene::Entity entity);
    void pause(scene::Entity entity);
    void resume(scene::Entity entity);
    [[nodiscard]] bool isPlaying(scene::Entity entity) const;
    // Seconds into the clip, and where to continue from.
    [[nodiscard]] float time(scene::Entity entity) const;
    void setTime(scene::Scene& scene, scene::Entity entity, float seconds);

    // Pauses every animator, as the editor does when the game pauses.
    void setPaused(bool paused);
    [[nodiscard]] bool paused() const noexcept;

    // Animators that hold a clip, playing or paused.
    [[nodiscard]] std::size_t animatorCount() const noexcept;

private:
    struct Implementation;

    std::unique_ptr<Implementation> m_implementation;
};

// Poses the bones under an entity with one clip at one time, without any playback state: what the
// editor shows while it previews a clip. Bones are found by name under the entity.
void applyClip(scene::Scene& scene, scene::Entity entity, const Clip& clip, float time);

} // namespace devex::animation
