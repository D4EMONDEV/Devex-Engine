#pragma once

#include <devex/core/Export.hpp>

#include <devex/core/Error.hpp>
#include <devex/math/Math.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace devex::asset {

// The property of a joint that a channel animates.
enum class AnimationPath : std::uint8_t
{
    Translation = 1,
    Rotation = 2,
    Scale = 3,
};

// How values are read between two keys, as glTF defines them.
enum class AnimationInterpolation : std::uint8_t
{
    Linear = 1,
    // The value of the previous key, until the next one.
    Step = 2,
    // Each key holds its incoming tangent, its value and its outgoing tangent.
    CubicSpline = 3,
};

// Floats per value: three for a translation or a scale, four for a rotation.
[[nodiscard]] DEVEX_API std::size_t componentCount(AnimationPath path) noexcept;

[[nodiscard]] DEVEX_API std::string_view toString(AnimationPath path) noexcept;
[[nodiscard]] DEVEX_API std::string_view toString(AnimationInterpolation interpolation) noexcept;

// The keys of one property of one joint, in seconds from the start of the clip.
struct DEVEX_API AnimationChannel
{
    // Index into AnimationClipData::joints.
    std::uint32_t joint = 0;
    AnimationPath path = AnimationPath::Translation;
    AnimationInterpolation interpolation = AnimationInterpolation::Linear;
    // Increasing, at least one.
    std::vector<float> times;
    // componentCount(path) floats per key, three times that for a cubic spline.
    std::vector<float> values;
};

// A moment of a clip that game code hears of when the clip plays past it, as the method tracks of
// Godot: a step, a blow that lands, a sound.
struct DEVEX_API AnimationEvent
{
    // Seconds from the start of the clip.
    float time = 0.0f;
    std::string name;

    bool operator==(const AnimationEvent&) const = default;
};

// An animation of a skeleton: what each joint does over time. Joints are named rather than
// numbered, so that a clip plays on any skeleton whose bones carry the same names.
struct DEVEX_API AnimationClipData
{
    std::string name;
    // Seconds, the time of the last key of the clip.
    float duration = 0.0f;
    std::vector<std::string> joints;
    std::vector<AnimationChannel> channels;
    // In the order of their times, which lie within the clip. The import settings of the model give
    // them, since its file has none.
    std::vector<AnimationEvent> events;
};

// Checks that joints exist, that times increase and that values match the keys and the path, and
// that events are named, in order, and within the clip.
[[nodiscard]] DEVEX_API core::Result<void> validate(const AnimationClipData& clip);

} // namespace devex::asset
