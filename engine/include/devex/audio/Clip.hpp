#pragma once

#include <devex/asset/AudioClipData.hpp>
#include <devex/core/Error.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace devex::audio {

// Slices in the waveform of a clip.
inline constexpr std::size_t waveformSize = 512;

// What decoding a whole file tells about it.
struct ClipInfo
{
    asset::AudioEncoding encoding = asset::AudioEncoding::Wav;
    std::uint32_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint64_t frames = 0;
    std::vector<std::uint8_t> waveform;
};

// Recognizes a .wav, .flac, .mp3 or .ogg (Vorbis) file by its first bytes and decodes it whole, as
// the import does. Fails for other files and for files that do not decode.
[[nodiscard]] core::Result<ClipInfo> probeClip(std::span<const std::byte> file);

// A clip ready to play, shared by every sound that plays it: the file, and the samples of a decoded
// clip.
class Clip
{
public:
    // Decodes the samples of a clip whose loading is Decoded.
    [[nodiscard]] static core::Result<std::shared_ptr<const Clip>> create(asset::AudioClipData data);

    [[nodiscard]] const asset::AudioClipData& data() const noexcept
    {
        return m_data;
    }

    // Interleaved samples of a decoded clip; empty for a streamed one.
    [[nodiscard]] std::span<const float> samples() const noexcept
    {
        return m_samples;
    }

private:
    Clip() = default;

    asset::AudioClipData m_data;
    std::vector<float> m_samples;
};

} // namespace devex::audio
