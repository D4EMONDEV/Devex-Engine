#pragma once

#include <devex/audio/Clip.hpp>
#include <devex/core/Error.hpp>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <miniaudio.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <memory>

namespace devex::audio::detail {

// A clip playing: the source of its samples and miniaudio's sound, which must not move once
// created. A decoded clip is read from its shared samples, a streamed one decoded from its file.
class Voice
{
public:
    // The sound starts stopped, in the group node (or the engine's endpoint when null).
    [[nodiscard]] static core::Result<std::unique_ptr<Voice>> create(ma_engine& engine, std::shared_ptr<const Clip> clip,
                                                                     ma_sound_group* group);
    ~Voice();

    Voice(const Voice&) = delete;
    Voice& operator=(const Voice&) = delete;

    [[nodiscard]] ma_sound& sound() noexcept
    {
        return m_sound;
    }

    [[nodiscard]] const ma_sound& sound() const noexcept
    {
        return m_sound;
    }

    [[nodiscard]] const std::shared_ptr<const Clip>& clip() const noexcept
    {
        return m_clip;
    }

private:
    Voice() = default;

    std::shared_ptr<const Clip> m_clip;
    ma_audio_buffer_ref m_buffer{};
    ma_decoder m_decoder{};
    bool m_hasBuffer = false;
    bool m_hasDecoder = false;
    bool m_hasSound = false;
    ma_sound m_sound{};
};

// miniaudio's decoder for the encoding of a clip.
[[nodiscard]] ma_encoding_format encodingFormat(asset::AudioEncoding encoding) noexcept;

} // namespace devex::audio::detail
