#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace devex::asset {

// The format of the file a clip was imported from. The values are stored in cooked files: never
// reorder them.
enum class AudioEncoding : std::uint8_t
{
    Wav = 1,
    Flac = 2,
    Mp3 = 3,
    Vorbis = 4,
};

// How a clip plays. The values are stored in cooked files: never reorder them.
enum class AudioLoading : std::uint8_t
{
    // Decoded once when it loads: for short sounds played often, at the cost of memory.
    Decoded = 1,
    // Decoded while it plays, from the file kept in memory: for music and long sounds.
    Streamed = 2,
};

[[nodiscard]] std::string_view toString(AudioEncoding encoding) noexcept;
[[nodiscard]] std::string_view toString(AudioLoading loading) noexcept;

// A sound: the file it was imported from, kept as it is, with what the engine and the editor need
// to know about it without decoding it.
struct AudioClipData
{
    AudioEncoding encoding = AudioEncoding::Wav;
    AudioLoading loading = AudioLoading::Decoded;
    std::uint32_t channels = 0;
    std::uint32_t sampleRate = 0;
    // The length in frames, one sample per channel each.
    std::uint64_t frames = 0;
    // The loudest sample of each slice of the clip, from 0 to 255, for the waveform the editor draws.
    std::vector<std::uint8_t> waveform;
    // The bytes of the source file.
    std::vector<std::byte> encoded;

    [[nodiscard]] double seconds() const noexcept
    {
        return sampleRate != 0 ? static_cast<double>(frames) / sampleRate : 0.0;
    }
};

} // namespace devex::asset
