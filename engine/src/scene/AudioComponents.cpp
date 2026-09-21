#include <devex/scene/AudioComponents.hpp>

namespace devex::scene {

DEVEX_REFLECT(AudioSource)
{
    type.field("clip", &AudioSource::clip, {.assetType = "audio"})
        .field("volume", &AudioSource::volume)
        .field("pitch", &AudioSource::pitch)
        .field("loop", &AudioSource::loop)
        .field("play_on_start", &AudioSource::playOnStart)
        .field("spatial", &AudioSource::spatial)
        .field("min_distance", &AudioSource::minDistance)
        .field("max_distance", &AudioSource::maxDistance)
        .field("attenuation", &AudioSource::attenuation)
        .field("rolloff", &AudioSource::rolloff)
        .field("doppler", &AudioSource::doppler)
        .field("group", &AudioSource::group, {.audioGroup = true});
}

DEVEX_REFLECT(AudioListener)
{
    static_cast<void>(type);
}

} // namespace devex::scene
