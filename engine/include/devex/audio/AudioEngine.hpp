#pragma once

#include <devex/asset/Project.hpp>
#include <devex/audio/Clip.hpp>
#include <devex/core/Error.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

// Sound, played with miniaudio: one engine per application mixes every sound into the output
// device, through the groups of the project (Effects, Music, Voice...), each with its volume.
namespace devex::audio {

class AudioEngine
{
public:
    struct Config
    {
        // Without a device, the engine mixes only when read() asks, as tests do.
        bool device = true;
        std::uint32_t sampleRate = 48000;
        std::uint32_t channels = 2;
    };

    // Falls back to an engine without a device, with a warning, when the system has no sound output.
    [[nodiscard]] static core::Result<std::unique_ptr<AudioEngine>> create(Config config);

    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    [[nodiscard]] bool hasDevice() const noexcept;
    [[nodiscard]] std::uint32_t sampleRate() const noexcept;
    [[nodiscard]] std::uint32_t channels() const noexcept;

    // Takes the groups and volumes of a project. A sound keeps its group by index.
    void configure(const asset::AudioSettings& settings);
    // The group of that name, "Master" excluded.
    [[nodiscard]] std::optional<std::uint32_t> findGroup(std::string_view name) const noexcept;
    void setGroupVolume(std::uint32_t group, float volume);
    [[nodiscard]] float groupVolume(std::uint32_t group) const noexcept;
    void setMasterVolume(float volume);
    [[nodiscard]] float masterVolume() const noexcept;

    // Mixes the next frames into interleaved samples, frames times channels, for an engine without
    // a device.
    void read(std::span<float> samples);

    // The clip the editor previews, straight into the master volume, outside any game.
    void preview(std::shared_ptr<const Clip> clip);
    void stopPreview();
    [[nodiscard]] bool isPreviewing() const;
    // Seconds into the previewed clip.
    [[nodiscard]] double previewSeconds() const;

    // The engine of miniaudio, for the audio module.
    [[nodiscard]] void* handle() noexcept;
    // The node of a group of miniaudio, for the audio module; the first group for an unknown index.
    [[nodiscard]] void* groupHandle(std::uint32_t group) noexcept;

private:
    struct Implementation;

    explicit AudioEngine(std::unique_ptr<Implementation> implementation) noexcept;

    std::unique_ptr<Implementation> m_implementation;
};

} // namespace devex::audio
