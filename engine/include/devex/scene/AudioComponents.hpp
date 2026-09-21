#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/reflection/Reflection.hpp>

#include <array>
#include <cstdint>
#include <string_view>

// Audio components: what the audio world plays. They hold data only; the Audio module plays their
// clips while a game runs.
namespace devex::scene {

// How a spatial sound fades with distance between its minimum and maximum distances.
enum class AudioAttenuation : std::uint8_t
{
    // As 1 / distance, as sounds do in the open.
    Inverse,
    // Linearly, down to silence at the maximum distance.
    Linear,
    // Faster than inverse, for sounds meant to stay local.
    Exponential,
    // Not at all: only the direction changes.
    None,
};

// Plays a clip at its entity. A spatial source is heard from where it is, fading with distance and
// panned between the speakers; a non-spatial one, such as music, is heard as it is. Code plays,
// stops and pauses it; with playOnStart it starts on its own when the game starts or the source
// appears.
struct AudioSource
{
    asset::AssetId clip;
    float volume = 1.0f;
    // 2 plays an octave higher and twice as fast.
    float pitch = 1.0f;
    bool loop = false;
    bool playOnStart = true;
    bool spatial = true;
    // In meters: full volume closer than the minimum, no more fading beyond the maximum.
    float minDistance = 1.0f;
    float maxDistance = 30.0f;
    AudioAttenuation attenuation = AudioAttenuation::Inverse;
    // How fast the volume fades: above 1 faster, below 1 slower.
    float rolloff = 1.0f;
    // The pitch shift of moving sources and listeners; 0 disables it.
    float doppler = 1.0f;
    // The group of the project the sound plays in, such as Effects or Music.
    std::uint32_t group = 0;
};
DEVEX_DECLARE_REFLECTION(AudioSource);

// Where the game is heard from. Without one, the primary camera listens.
struct AudioListener
{
};
DEVEX_DECLARE_REFLECTION(AudioListener);

} // namespace devex::scene

template <>
struct devex::reflection::EnumNames<devex::scene::AudioAttenuation>
{
    static constexpr std::array<std::string_view, 4> names{"inverse", "linear", "exponential", "none"};
};
