#include "Voice.hpp"

#include <devex/audio/Clip.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <string_view>
#include <utility>

namespace devex::audio {
namespace {

// Frames decoded at a time.
constexpr ma_uint64 chunkFrames = 4096;
// Frames per slice of the fine waveform, which the final one keeps the peaks of.
constexpr std::uint64_t sliceFrames = 64;

[[nodiscard]] std::optional<asset::AudioEncoding> recognize(std::span<const std::byte> file) noexcept
{
    const auto startsWith = [&](std::string_view magic, std::size_t at = 0) {
        return file.size() >= at + magic.size() && std::memcmp(file.data() + at, magic.data(), magic.size()) == 0;
    };
    if (startsWith("RIFF") && startsWith("WAVE", 8))
    {
        return asset::AudioEncoding::Wav;
    }
    if (startsWith("fLaC"))
    {
        return asset::AudioEncoding::Flac;
    }
    if (startsWith("OggS"))
    {
        return asset::AudioEncoding::Vorbis;
    }
    // An ID3 tag, or the sync word of an MPEG audio frame.
    if (startsWith("ID3") ||
        (file.size() >= 2 && file[0] == std::byte{0xFF} && (std::to_integer<unsigned>(file[1]) & 0xE0u) == 0xE0u))
    {
        return asset::AudioEncoding::Mp3;
    }
    return std::nullopt;
}

[[nodiscard]] core::Result<void> openDecoder(std::span<const std::byte> file, asset::AudioEncoding encoding,
                                             ma_decoder& decoder)
{
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    config.encodingFormat = detail::encodingFormat(encoding);
    if (ma_decoder_init_memory(file.data(), file.size(), &config, &decoder) != MA_SUCCESS)
    {
        return core::makeError(core::ErrorCode::Parse, "the {} file does not decode", asset::toString(encoding));
    }
    return {};
}

} // namespace

namespace detail {

ma_encoding_format encodingFormat(asset::AudioEncoding encoding) noexcept
{
    switch (encoding)
    {
    case asset::AudioEncoding::Wav:
        return ma_encoding_format_wav;
    case asset::AudioEncoding::Flac:
        return ma_encoding_format_flac;
    case asset::AudioEncoding::Mp3:
        return ma_encoding_format_mp3;
    case asset::AudioEncoding::Vorbis:
        return ma_encoding_format_vorbis;
    }
    return ma_encoding_format_unknown;
}

core::Result<std::unique_ptr<Voice>> Voice::create(ma_engine& engine, std::shared_ptr<const Clip> clip,
                                                   ma_sound_group* group)
{
    std::unique_ptr<Voice> voice(new Voice());
    voice->m_clip = std::move(clip);
    const asset::AudioClipData& data = voice->m_clip->data();
    ma_data_source* source = nullptr;
    if (!voice->m_clip->samples().empty())
    {
        const std::span<const float> samples = voice->m_clip->samples();
        if (ma_audio_buffer_ref_init(ma_format_f32, data.channels, samples.data(), samples.size() / data.channels,
                                     &voice->m_buffer) != MA_SUCCESS)
        {
            return core::makeError(core::ErrorCode::InvalidState, "cannot play the samples of the clip");
        }
        voice->m_hasBuffer = true;
        // miniaudio leaves the rate unset, which would play the clip at the rate of the engine.
        voice->m_buffer.sampleRate = data.sampleRate;
        source = &voice->m_buffer;
    }
    else
    {
        if (core::Result<void> opened = openDecoder(data.encoded, data.encoding, voice->m_decoder); !opened)
        {
            return std::unexpected(opened.error());
        }
        voice->m_hasDecoder = true;
        source = &voice->m_decoder;
    }
    if (ma_sound_init_from_data_source(&engine, source, 0, group, &voice->m_sound) != MA_SUCCESS)
    {
        return core::makeError(core::ErrorCode::InvalidState, "cannot create a sound for the clip");
    }
    voice->m_hasSound = true;
    return voice;
}

Voice::~Voice()
{
    if (m_hasSound)
    {
        ma_sound_uninit(&m_sound);
    }
    if (m_hasDecoder)
    {
        ma_decoder_uninit(&m_decoder);
    }
    if (m_hasBuffer)
    {
        ma_audio_buffer_ref_uninit(&m_buffer);
    }
}

} // namespace detail

core::Result<ClipInfo> probeClip(std::span<const std::byte> file)
{
    const std::optional<asset::AudioEncoding> encoding = recognize(file);
    if (!encoding)
    {
        return core::makeError(core::ErrorCode::Unsupported, "not a WAV, FLAC, MP3 or Ogg Vorbis file");
    }
    ma_decoder decoder;
    if (core::Result<void> opened = openDecoder(file, *encoding, decoder); !opened)
    {
        return std::unexpected(opened.error());
    }
    ClipInfo info{.encoding = *encoding, .channels = decoder.outputChannels, .sampleRate = decoder.outputSampleRate};

    // The loudest sample of each slice of 64 frames, then of each part of the final waveform.
    std::vector<float> chunk(static_cast<std::size_t>(chunkFrames) * info.channels);
    std::vector<float> slices;
    float slicePeak = 0.0f;
    std::uint64_t sliceCount = 0;
    for (;;)
    {
        ma_uint64 read = 0;
        const ma_result result = ma_decoder_read_pcm_frames(&decoder, chunk.data(), chunkFrames, &read);
        for (ma_uint64 frame = 0; frame < read; ++frame)
        {
            for (std::uint32_t channel = 0; channel < info.channels; ++channel)
            {
                slicePeak = std::max(slicePeak, std::abs(chunk[frame * info.channels + channel]));
            }
            if (++sliceCount == sliceFrames)
            {
                slices.push_back(slicePeak);
                slicePeak = 0.0f;
                sliceCount = 0;
            }
        }
        info.frames += read;
        if (result != MA_SUCCESS || read == 0)
        {
            break;
        }
    }
    ma_decoder_uninit(&decoder);
    if (sliceCount > 0)
    {
        slices.push_back(slicePeak);
    }
    if (info.frames == 0 || info.channels == 0 || info.sampleRate == 0)
    {
        return core::makeError(core::ErrorCode::Parse, "the {} file holds no sound", asset::toString(*encoding));
    }

    info.waveform.resize(waveformSize);
    for (std::size_t index = 0; index < waveformSize; ++index)
    {
        const std::size_t first = index * slices.size() / waveformSize;
        const std::size_t last = std::max(first + 1, (index + 1) * slices.size() / waveformSize);
        float peak = 0.0f;
        for (std::size_t slice = first; slice < std::min(last, slices.size()); ++slice)
        {
            peak = std::max(peak, slices[slice]);
        }
        info.waveform[index] = static_cast<std::uint8_t>(std::lround(std::clamp(peak, 0.0f, 1.0f) * 255.0f));
    }
    return info;
}

core::Result<std::shared_ptr<const Clip>> Clip::create(asset::AudioClipData data)
{
    std::shared_ptr<Clip> clip(new Clip());
    clip->m_data = std::move(data);
    if (clip->m_data.loading != asset::AudioLoading::Decoded)
    {
        return std::shared_ptr<const Clip>(std::move(clip));
    }
    ma_decoder decoder;
    if (core::Result<void> opened = openDecoder(clip->m_data.encoded, clip->m_data.encoding, decoder); !opened)
    {
        return std::unexpected(opened.error());
    }
    if (decoder.outputChannels != clip->m_data.channels)
    {
        ma_decoder_uninit(&decoder);
        return core::makeError(core::ErrorCode::Parse, "the clip has {} channels instead of {}", decoder.outputChannels,
                               clip->m_data.channels);
    }
    clip->m_samples.reserve(static_cast<std::size_t>(clip->m_data.frames) * clip->m_data.channels);
    std::vector<float> chunk(static_cast<std::size_t>(chunkFrames) * clip->m_data.channels);
    for (;;)
    {
        ma_uint64 read = 0;
        const ma_result result = ma_decoder_read_pcm_frames(&decoder, chunk.data(), chunkFrames, &read);
        clip->m_samples.insert(clip->m_samples.end(), chunk.begin(),
                               chunk.begin() + static_cast<std::ptrdiff_t>(read * clip->m_data.channels));
        if (result != MA_SUCCESS || read == 0)
        {
            break;
        }
    }
    ma_decoder_uninit(&decoder);
    if (clip->m_samples.empty())
    {
        return core::makeError(core::ErrorCode::Parse, "the clip holds no sound");
    }
    return std::shared_ptr<const Clip>(std::move(clip));
}

} // namespace devex::audio
