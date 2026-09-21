#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/audio/AudioEngine.hpp>
#include <devex/core/Time.hpp>
#include <devex/math/Math.hpp>
#include <devex/scene/Entity.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace devex::scene {
class Scene;
}

namespace devex::audio {

// The sounds of a game that plays: those of the AudioSource components of its scene, which follow
// their entities, and one-shot sounds played by code. The listener is the entity with an
// AudioListener, or else the primary camera.
class AudioWorld
{
public:
    // The clip of an asset, loaded once and shared; null when it cannot be loaded.
    using ClipSource = std::function<std::shared_ptr<const Clip>(asset::AssetId clip)>;

    AudioWorld(AudioEngine& engine, ClipSource clips);
    ~AudioWorld();

    AudioWorld(const AudioWorld&) = delete;
    AudioWorld& operator=(const AudioWorld&) = delete;

    // Once per frame, after the world transforms are updated: sources that appear with playOnStart
    // start, sounds follow their entities and settings, the sounds of removed sources stop and
    // finished one-shots go. Using the world with another scene starts over from that scene.
    void update(scene::Scene& scene, core::Duration delta);

    // Plays the clip of the AudioSource of the entity from its start.
    void play(scene::Scene& scene, scene::Entity entity);
    void stop(scene::Entity entity);
    void pause(scene::Entity entity);
    // Goes on from where pause stopped.
    void resume(scene::Entity entity);
    [[nodiscard]] bool isPlaying(scene::Entity entity) const;

    // A clip played once at a position, in a group, without an entity. Non-spatial sounds ignore the
    // position.
    void playOneShot(asset::AssetId clip, math::Vec3 position, float volume = 1.0f, std::uint32_t group = 0,
                     bool spatial = true);

    // Pauses every sound of the world, as the editor does when the game pauses.
    void setPaused(bool paused);
    [[nodiscard]] bool paused() const noexcept;

    // Sounds that play or wait to resume.
    [[nodiscard]] std::size_t soundCount() const noexcept;

    [[nodiscard]] AudioEngine& engine() noexcept;

private:
    struct Implementation;

    std::unique_ptr<Implementation> m_implementation;
};

} // namespace devex::audio
