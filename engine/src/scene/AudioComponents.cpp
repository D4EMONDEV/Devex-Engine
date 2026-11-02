#include <devex/scene/AudioComponents.hpp>

namespace devex::scene {

DEVEX_REFLECT(AudioSource)
{
    type.field("clip", &AudioSource::clip, {.assetType = "audio", .group = "Stream"})
        .field("volume", &AudioSource::volume, {.group = "Playback"})
        .field("pitch", &AudioSource::pitch)
        .field("loop", &AudioSource::loop)
        .field("play_on_start", &AudioSource::playOnStart)
        .field("spatial", &AudioSource::spatial, {.group = "Spatial"})
        .field("min_distance", &AudioSource::minDistance)
        .field("max_distance", &AudioSource::maxDistance)
        .field("attenuation", &AudioSource::attenuation)
        .field("rolloff", &AudioSource::rolloff)
        .field("doppler", &AudioSource::doppler)
        .field("group", &AudioSource::group, {.audioGroup = true, .group = "Routing"});
}

DEVEX_REFLECT(AudioListener)
{
    static_cast<void>(type);
}

} // namespace devex::scene
