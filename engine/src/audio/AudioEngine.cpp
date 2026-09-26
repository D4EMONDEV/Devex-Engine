#include "Voice.hpp"

#include <devex/audio/AudioEngine.hpp>
#include <devex/core/Log.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace devex::audio {

struct AudioEngine::Implementation
{
    ma_engine engine{};
    bool initialized = false;
    bool device = false;
    // Groups exist from the start, at stable addresses, so that sounds can always play in them.
    std::array<std::unique_ptr<ma_sound_group>, asset::audioGroupCount> groups;
    std::array<std::string, asset::audioGroupCount> names;
    // The volumes of the game, and those of the player that multiply them.
    std::array<float, asset::audioGroupCount> volumes = [] {
        std::array<float, asset::audioGroupCount> all{};
        all.fill(1.0f);
        return all;
    }();
    std::array<float, asset::audioGroupCount> playerVolumes = volumes;
    float masterVolume = 1.0f;
    float playerMasterVolume = 1.0f;
    std::unique_ptr<detail::Voice> preview;

    ~Implementation()
    {
        preview.reset();
        for (std::unique_ptr<ma_sound_group>& group : groups)
        {
            if (group != nullptr)
            {
                ma_sound_group_uninit(group.get());
            }
        }
        if (initialized)
        {
            ma_engine_uninit(&engine);
        }
    }
};

AudioEngine::AudioEngine(std::unique_ptr<Implementation> implementation) noexcept
    : m_implementation(std::move(implementation))
{
}

AudioEngine::~AudioEngine() = default;

core::Result<std::unique_ptr<AudioEngine>> AudioEngine::create(Config config)
{
    auto implementation = std::make_unique<Implementation>();
    ma_engine_config engineConfig = ma_engine_config_init();
    if (config.device)
    {
        if (ma_engine_init(&engineConfig, &implementation->engine) == MA_SUCCESS)
        {
            implementation->device = true;
        }
        else
        {
            DEVEX_LOG_WARNING("No sound output could be opened: the game plays without sound");
        }
    }
    if (!implementation->device)
    {
        // Without a device, the format of the mix has to be given.
        engineConfig.noDevice = MA_TRUE;
        engineConfig.channels = std::max(config.channels, 1u);
        engineConfig.sampleRate = std::max(config.sampleRate, 8000u);
        if (ma_engine_init(&engineConfig, &implementation->engine) != MA_SUCCESS)
        {
            return core::makeError(core::ErrorCode::Platform, "cannot start the audio mixer");
        }
    }
    implementation->initialized = true;
    for (std::unique_ptr<ma_sound_group>& group : implementation->groups)
    {
        group = std::make_unique<ma_sound_group>();
        if (ma_sound_group_init(&implementation->engine, 0, nullptr, group.get()) != MA_SUCCESS)
        {
            group.reset();
            return core::makeError(core::ErrorCode::Platform, "cannot create the audio groups");
        }
    }
    std::unique_ptr<AudioEngine> engine(new AudioEngine(std::move(implementation)));
    engine->configure({});
    if (engine->hasDevice())
    {
        DEVEX_LOG_DEBUG("Audio output: {} channels at {} Hz", engine->channels(), engine->sampleRate());
    }
    return engine;
}

bool AudioEngine::hasDevice() const noexcept
{
    return m_implementation->device;
}

std::uint32_t AudioEngine::sampleRate() const noexcept
{
    return ma_engine_get_sample_rate(&m_implementation->engine);
}

std::uint32_t AudioEngine::channels() const noexcept
{
    return ma_engine_get_channels(&m_implementation->engine);
}

void AudioEngine::configure(const asset::AudioSettings& settings)
{
    setMasterVolume(settings.masterVolume);
    for (std::size_t index = 0; index < asset::audioGroupCount; ++index)
    {
        m_implementation->names[index] = settings.groupNames[index];
        setGroupVolume(static_cast<std::uint32_t>(index), settings.groupVolumes[index]);
    }
}

std::optional<std::uint32_t> AudioEngine::findGroup(std::string_view name) const noexcept
{
    for (std::size_t index = 0; index < asset::audioGroupCount; ++index)
    {
        if (!name.empty() && m_implementation->names[index] == name)
        {
            return static_cast<std::uint32_t>(index);
        }
    }
    return std::nullopt;
}

void AudioEngine::setGroupVolume(std::uint32_t group, float volume)
{
    if (group < asset::audioGroupCount)
    {
        Implementation& state = *m_implementation;
        state.volumes[group] = std::max(volume, 0.0f);
        ma_sound_group_set_volume(state.groups[group].get(), state.volumes[group] * state.playerVolumes[group]);
    }
}

float AudioEngine::groupVolume(std::uint32_t group) const noexcept
{
    return group < asset::audioGroupCount ? m_implementation->volumes[group] : 0.0f;
}

void AudioEngine::setMasterVolume(float volume)
{
    Implementation& state = *m_implementation;
    state.masterVolume = std::max(volume, 0.0f);
    ma_engine_set_volume(&state.engine, state.masterVolume * state.playerMasterVolume);
}

float AudioEngine::masterVolume() const noexcept
{
    return m_implementation->masterVolume;
}

void AudioEngine::setPlayerMasterVolume(float volume)
{
    m_implementation->playerMasterVolume = std::max(volume, 0.0f);
    setMasterVolume(m_implementation->masterVolume);
}

float AudioEngine::playerMasterVolume() const noexcept
{
    return m_implementation->playerMasterVolume;
}

void AudioEngine::setPlayerGroupVolume(std::uint32_t group, float volume)
{
    if (group < asset::audioGroupCount)
    {
        m_implementation->playerVolumes[group] = std::max(volume, 0.0f);
        setGroupVolume(group, m_implementation->volumes[group]);
    }
}

float AudioEngine::playerGroupVolume(std::uint32_t group) const noexcept
{
    return group < asset::audioGroupCount ? m_implementation->playerVolumes[group] : 0.0f;
}

void AudioEngine::read(std::span<float> samples)
{
    const ma_uint32 channelCount = channels();
    if (m_implementation->device || channelCount == 0)
    {
        return;
    }
    ma_uint64 read = 0;
    ma_engine_read_pcm_frames(&m_implementation->engine, samples.data(), samples.size() / channelCount, &read);
    std::fill(samples.begin() + static_cast<std::ptrdiff_t>(read * channelCount), samples.end(), 0.0f);
}

void AudioEngine::preview(std::shared_ptr<const Clip> clip)
{
    stopPreview();
    if (clip == nullptr)
    {
        return;
    }
    core::Result<std::unique_ptr<detail::Voice>> voice = detail::Voice::create(m_implementation->engine, std::move(clip), nullptr);
    if (!voice)
    {
        DEVEX_LOG_WARNING("Cannot preview the clip: {}", voice.error());
        return;
    }
    m_implementation->preview = std::move(*voice);
    ma_sound_set_spatialization_enabled(&m_implementation->preview->sound(), MA_FALSE);
    ma_sound_start(&m_implementation->preview->sound());
}

void AudioEngine::stopPreview()
{
    m_implementation->preview.reset();
}

bool AudioEngine::isPreviewing() const
{
    return m_implementation->preview != nullptr && ma_sound_is_playing(&m_implementation->preview->sound()) &&
           !ma_sound_at_end(&m_implementation->preview->sound());
}

double AudioEngine::previewSeconds() const
{
    if (m_implementation->preview == nullptr)
    {
        return 0.0;
    }
    float seconds = 0.0f;
    ma_sound_get_cursor_in_seconds(&m_implementation->preview->sound(), &seconds);
    return seconds;
}

void* AudioEngine::handle() noexcept
{
    return &m_implementation->engine;
}

void* AudioEngine::groupHandle(std::uint32_t group) noexcept
{
    return m_implementation->groups[group < asset::audioGroupCount ? group : 0].get();
}

} // namespace devex::audio
