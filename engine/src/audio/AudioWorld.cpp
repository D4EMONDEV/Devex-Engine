#include "Voice.hpp"

#include <devex/audio/AudioWorld.hpp>
#include <devex/core/Log.hpp>
#include <devex/scene/AudioComponents.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/Scene.hpp>

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace devex::audio {
namespace {

using detail::Voice;
using scene::AudioSource;
using scene::Entity;

[[nodiscard]] std::uint64_t keyOf(Entity entity) noexcept
{
    return static_cast<std::uint64_t>(entity.generation) << 32 | entity.index;
}

[[nodiscard]] ma_attenuation_model attenuationModel(scene::AudioAttenuation attenuation) noexcept
{
    switch (attenuation)
    {
    case scene::AudioAttenuation::Inverse:
        return ma_attenuation_model_inverse;
    case scene::AudioAttenuation::Linear:
        return ma_attenuation_model_linear;
    case scene::AudioAttenuation::Exponential:
        return ma_attenuation_model_exponential;
    case scene::AudioAttenuation::None:
        return ma_attenuation_model_none;
    }
    return ma_attenuation_model_inverse;
}

// Where an entity is in the world, as of the last update of the transforms.
[[nodiscard]] std::optional<math::Mat4> worldMatrixOf(const scene::Scene& scene, Entity entity)
{
    if (const scene::WorldTransform* const world = scene.tryGet<scene::WorldTransform>(entity))
    {
        return world->matrix;
    }
    if (const scene::Transform* const transform = scene.tryGet<scene::Transform>(entity))
    {
        return math::composeTrs({transform->position, transform->rotation, transform->scale});
    }
    return std::nullopt;
}

// A source of the scene and its sound, if it has one.
struct SourceState
{
    Entity entity;
    asset::AssetId clip;
    std::unique_ptr<Voice> voice;
    std::uint32_t group = 0;
    // Plays, or would play but for a pause of the world.
    bool playing = false;
    math::Vec3 position{0.0f};
    bool placed = false;
};

} // namespace

struct AudioWorld::Implementation
{
    AudioEngine& engine;
    ClipSource clips;
    const scene::Scene* scene = nullptr;
    std::unordered_map<std::uint64_t, SourceState> sources;
    std::vector<std::unique_ptr<Voice>> oneShots;
    std::unordered_set<asset::AssetId> missingClips;
    bool paused = false;
    math::Vec3 listenerPosition{0.0f};
    bool listenerPlaced = false;

    [[nodiscard]] ma_engine& maEngine() noexcept
    {
        return *static_cast<ma_engine*>(engine.handle());
    }

    [[nodiscard]] std::shared_ptr<const Clip> clip(asset::AssetId id)
    {
        if (!id.isValid())
        {
            return nullptr;
        }
        std::shared_ptr<const Clip> loaded = clips ? clips(id) : nullptr;
        if (loaded == nullptr && missingClips.insert(id).second)
        {
            DEVEX_LOG_WARNING("The audio clip {} cannot be loaded", id.uuid);
        }
        return loaded;
    }

    [[nodiscard]] std::unique_ptr<Voice> createVoice(asset::AssetId id, std::uint32_t group)
    {
        std::shared_ptr<const Clip> loaded = clip(id);
        if (loaded == nullptr)
        {
            return nullptr;
        }
        core::Result<std::unique_ptr<Voice>> voice =
            Voice::create(maEngine(), std::move(loaded), static_cast<ma_sound_group*>(engine.groupHandle(group)));
        if (!voice)
        {
            DEVEX_LOG_WARNING("Cannot play the audio clip {}: {}", id.uuid, voice.error());
            return nullptr;
        }
        return std::move(*voice);
    }

    static void apply(Voice& voice, const AudioSource& source, math::Vec3 position, math::Vec3 velocity)
    {
        ma_sound& sound = voice.sound();
        ma_sound_set_volume(&sound, std::max(source.volume, 0.0f));
        ma_sound_set_pitch(&sound, std::max(source.pitch, 0.01f));
        ma_sound_set_looping(&sound, source.loop ? MA_TRUE : MA_FALSE);
        ma_sound_set_spatialization_enabled(&sound, source.spatial ? MA_TRUE : MA_FALSE);
        ma_sound_set_position(&sound, position.x, position.y, position.z);
        ma_sound_set_velocity(&sound, velocity.x, velocity.y, velocity.z);
        ma_sound_set_min_distance(&sound, std::max(source.minDistance, 0.0f));
        ma_sound_set_max_distance(&sound, std::max(source.maxDistance, source.minDistance));
        ma_sound_set_attenuation_model(&sound, attenuationModel(source.attenuation));
        ma_sound_set_rolloff(&sound, std::max(source.rolloff, 0.0f));
        ma_sound_set_doppler_factor(&sound, std::max(source.doppler, 0.0f));
    }

    // Starts the clip of a source from its beginning.
    void start(SourceState& state, const AudioSource& source)
    {
        state.voice.reset();
        state.clip = source.clip;
        state.group = source.group;
        state.voice = createVoice(source.clip, source.group);
        state.playing = state.voice != nullptr;
        if (state.voice == nullptr)
        {
            return;
        }
        apply(*state.voice, source, state.position, math::Vec3{0.0f});
        if (!paused)
        {
            ma_sound_start(&state.voice->sound());
        }
    }

    void useScene(const scene::Scene& nextScene)
    {
        // Sounds played by Start belong to the first scene, even before its first update.
        if (scene != nullptr && scene != &nextScene)
        {
            sources.clear();
            oneShots.clear();
            listenerPlaced = false;
        }
        scene = &nextScene;
    }
};

AudioWorld::AudioWorld(AudioEngine& engine, ClipSource clips)
    : m_implementation(std::make_unique<Implementation>(Implementation{.engine = engine, .clips = std::move(clips)}))
{
}

AudioWorld::~AudioWorld() = default;

void AudioWorld::update(scene::Scene& scene, core::Duration delta)
{
    Implementation& world = *m_implementation;
    world.useScene(scene);
    const float seconds = static_cast<float>(delta.count());

    std::unordered_set<std::uint64_t> seen;
    for ([[maybe_unused]] auto [entity, source] : scene.view<AudioSource>())
    {
        const std::uint64_t key = keyOf(entity);
        seen.insert(key);
        const std::optional<math::Mat4> matrix = worldMatrixOf(scene, entity);
        const math::Vec3 position = matrix ? math::Vec3((*matrix)[3]) : math::Vec3{0.0f};
        auto [found, appeared] = world.sources.try_emplace(key);
        SourceState& state = found->second;
        const math::Vec3 velocity =
            state.placed && seconds > 0.0f ? (position - state.position) / seconds : math::Vec3{0.0f};
        state.entity = entity;
        state.position = position;
        state.placed = true;
        if (appeared)
        {
            state.clip = source.clip;
            state.group = source.group;
            if (source.playOnStart)
            {
                world.start(state, source);
            }
            continue;
        }
        // Another clip or group: the new clip starts over, as the old one is gone.
        if (state.voice != nullptr && (state.clip != source.clip || state.group != source.group))
        {
            if (state.playing && state.clip == source.clip)
            {
                // Only the group changed: the sound goes on in the new group.
                float cursor = 0.0f;
                ma_sound_get_cursor_in_seconds(&state.voice->sound(), &cursor);
                world.start(state, source);
                if (state.voice != nullptr)
                {
                    ma_sound_seek_to_pcm_frame(&state.voice->sound(),
                                               static_cast<ma_uint64>(cursor * static_cast<float>(state.voice->clip()->data().sampleRate)));
                }
            }
            else
            {
                state.voice.reset();
                state.playing = false;
            }
        }
        state.clip = source.clip;
        state.group = source.group;
        if (state.voice == nullptr)
        {
            continue;
        }
        Implementation::apply(*state.voice, source, position, velocity);
        // A clip that does not loop stops at its end.
        if (state.playing && ma_sound_at_end(&state.voice->sound()))
        {
            state.playing = false;
        }
    }
    std::erase_if(world.sources, [&](const auto& entry) { return !seen.contains(entry.first); });
    std::erase_if(world.oneShots, [](const std::unique_ptr<Voice>& voice) { return ma_sound_at_end(&voice->sound()); });

    // The listener: an entity with an AudioListener, or the primary camera.
    std::optional<math::Mat4> listener;
    for ([[maybe_unused]] auto [entity, marker] : scene.view<scene::AudioListener>())
    {
        if (!listener)
        {
            listener = worldMatrixOf(scene, entity);
        }
    }
    for ([[maybe_unused]] auto [entity, camera] : scene.view<scene::Camera>())
    {
        if (!listener && camera.primary)
        {
            listener = worldMatrixOf(scene, entity);
        }
    }
    ma_engine& engine = world.maEngine();
    if (listener)
    {
        const math::Vec3 position((*listener)[3]);
        const math::Vec3 forward = math::normalize(-math::Vec3((*listener)[2]));
        const math::Vec3 up = math::normalize(math::Vec3((*listener)[1]));
        const math::Vec3 velocity =
            world.listenerPlaced && seconds > 0.0f ? (position - world.listenerPosition) / seconds : math::Vec3{0.0f};
        ma_engine_listener_set_position(&engine, 0, position.x, position.y, position.z);
        ma_engine_listener_set_direction(&engine, 0, forward.x, forward.y, forward.z);
        ma_engine_listener_set_world_up(&engine, 0, up.x, up.y, up.z);
        ma_engine_listener_set_velocity(&engine, 0, velocity.x, velocity.y, velocity.z);
        world.listenerPosition = position;
        world.listenerPlaced = true;
    }
}

void AudioWorld::play(scene::Scene& scene, scene::Entity entity)
{
    const AudioSource* const source = scene.isAlive(entity) ? scene.tryGet<AudioSource>(entity) : nullptr;
    if (source == nullptr)
    {
        return;
    }
    Implementation& world = *m_implementation;
    world.useScene(scene);
    SourceState& state = world.sources[keyOf(entity)];
    state.entity = entity;
    if (!state.placed)
    {
        if (const std::optional<math::Mat4> matrix = worldMatrixOf(scene, entity))
        {
            state.position = math::Vec3((*matrix)[3]);
            state.placed = true;
        }
    }
    world.start(state, *source);
}

void AudioWorld::stop(scene::Entity entity)
{
    const auto found = m_implementation->sources.find(keyOf(entity));
    if (found != m_implementation->sources.end())
    {
        found->second.voice.reset();
        found->second.playing = false;
    }
}

void AudioWorld::pause(scene::Entity entity)
{
    const auto found = m_implementation->sources.find(keyOf(entity));
    if (found != m_implementation->sources.end() && found->second.voice != nullptr && found->second.playing)
    {
        ma_sound_stop(&found->second.voice->sound());
        found->second.playing = false;
    }
}

void AudioWorld::resume(scene::Entity entity)
{
    const auto found = m_implementation->sources.find(keyOf(entity));
    if (found != m_implementation->sources.end() && found->second.voice != nullptr && !found->second.playing &&
        !ma_sound_at_end(&found->second.voice->sound()))
    {
        found->second.playing = true;
        if (!m_implementation->paused)
        {
            ma_sound_start(&found->second.voice->sound());
        }
    }
}

bool AudioWorld::isPlaying(scene::Entity entity) const
{
    const auto found = m_implementation->sources.find(keyOf(entity));
    return found != m_implementation->sources.end() && found->second.voice != nullptr && found->second.playing &&
           !ma_sound_at_end(&found->second.voice->sound());
}

void AudioWorld::playOneShot(asset::AssetId clip, math::Vec3 position, float volume, std::uint32_t group, bool spatial)
{
    Implementation& world = *m_implementation;
    std::unique_ptr<Voice> voice = world.createVoice(clip, group);
    if (voice == nullptr)
    {
        return;
    }
    ma_sound& sound = voice->sound();
    ma_sound_set_volume(&sound, std::max(volume, 0.0f));
    ma_sound_set_spatialization_enabled(&sound, spatial ? MA_TRUE : MA_FALSE);
    ma_sound_set_position(&sound, position.x, position.y, position.z);
    if (!world.paused)
    {
        ma_sound_start(&sound);
    }
    world.oneShots.push_back(std::move(voice));
}

void AudioWorld::setPaused(bool paused)
{
    Implementation& world = *m_implementation;
    if (world.paused == paused)
    {
        return;
    }
    world.paused = paused;
    for (auto& [key, state] : world.sources)
    {
        if (state.voice != nullptr && state.playing)
        {
            paused ? ma_sound_stop(&state.voice->sound()) : ma_sound_start(&state.voice->sound());
        }
    }
    for (const std::unique_ptr<Voice>& voice : world.oneShots)
    {
        paused ? ma_sound_stop(&voice->sound()) : ma_sound_start(&voice->sound());
    }
}

bool AudioWorld::paused() const noexcept
{
    return m_implementation->paused;
}

std::size_t AudioWorld::soundCount() const noexcept
{
    std::size_t count = m_implementation->oneShots.size();
    for (const auto& [key, state] : m_implementation->sources)
    {
        count += state.voice != nullptr && state.playing ? 1 : 0;
    }
    return count;
}

AudioEngine& AudioWorld::engine() noexcept
{
    return m_implementation->engine;
}

} // namespace devex::audio
