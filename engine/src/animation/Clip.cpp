#include <devex/animation/Clip.hpp>

#include <algorithm>
#include <utility>

namespace devex::animation {
namespace {

using asset::AnimationChannel;
using asset::AnimationInterpolation;
using asset::AnimationPath;

// Where a time falls in the keys of a channel: the key before it and how far it is to the next one.
struct KeyRange
{
    std::size_t first = 0;
    std::size_t second = 0;
    float fraction = 0.0f;
};

[[nodiscard]] KeyRange findKeys(const AnimationChannel& channel, float time) noexcept
{
    const std::vector<float>& times = channel.times;
    if (times.size() == 1 || time <= times.front())
    {
        return {0, 0, 0.0f};
    }
    if (time >= times.back())
    {
        const std::size_t last = times.size() - 1;
        return {last, last, 0.0f};
    }
    // The first key after the time, which exists since the time is inside the range.
    const std::size_t second =
        static_cast<std::size_t>(std::ranges::upper_bound(times, time) - times.begin());
    const std::size_t first = second - 1;
    const float span = times[second] - times[first];
    return {first, second, span > 0.0f ? (time - times[first]) / span : 0.0f};
}

// The values of a key, with a cubic spline keeping only its value here.
[[nodiscard]] const float* keyValues(const AnimationChannel& channel, std::size_t key) noexcept
{
    const std::size_t components = asset::componentCount(channel.path);
    const bool spline = channel.interpolation == AnimationInterpolation::CubicSpline;
    const std::size_t stride = components * (spline ? 3 : 1);
    return channel.values.data() + key * stride + (spline ? components : 0);
}

// The tangents of a cubic spline key, scaled by the span between the two keys.
[[nodiscard]] math::Vec4 splineTangent(const AnimationChannel& channel, std::size_t key, bool outgoing,
                                       float span) noexcept
{
    const std::size_t components = asset::componentCount(channel.path);
    const float* const values =
        channel.values.data() + key * components * 3 + (outgoing ? components * 2 : 0);
    math::Vec4 tangent{0.0f};
    for (std::size_t index = 0; index < components; ++index)
    {
        tangent[static_cast<int>(index)] = values[index] * span;
    }
    return tangent;
}

[[nodiscard]] math::Vec4 valueOf(const AnimationChannel& channel, std::size_t key) noexcept
{
    const float* const values = keyValues(channel, key);
    math::Vec4 value{0.0f};
    for (std::size_t index = 0; index < asset::componentCount(channel.path); ++index)
    {
        value[static_cast<int>(index)] = values[index];
    }
    return value;
}

// The value of a channel at a time, as four floats (the fourth unused outside rotations).
[[nodiscard]] math::Vec4 sampleChannel(const AnimationChannel& channel, float time) noexcept
{
    const KeyRange keys = findKeys(channel, time);
    const math::Vec4 first = valueOf(channel, keys.first);
    if (keys.first == keys.second || channel.interpolation == AnimationInterpolation::Step)
    {
        return first;
    }
    const math::Vec4 second = valueOf(channel, keys.second);
    if (channel.interpolation == AnimationInterpolation::CubicSpline)
    {
        // The Hermite basis glTF defines, with tangents scaled by the span between the keys.
        const float span = channel.times[keys.second] - channel.times[keys.first];
        const math::Vec4 outgoing = splineTangent(channel, keys.first, true, span);
        const math::Vec4 incoming = splineTangent(channel, keys.second, false, span);
        const float t = keys.fraction;
        const float t2 = t * t;
        const float t3 = t2 * t;
        return first * (2.0f * t3 - 3.0f * t2 + 1.0f) + outgoing * (t3 - 2.0f * t2 + t) +
               second * (-2.0f * t3 + 3.0f * t2) + incoming * (t3 - t2);
    }
    if (channel.path == AnimationPath::Rotation)
    {
        const math::Quat from{first.w, first.x, first.y, first.z};
        const math::Quat to{second.w, second.x, second.y, second.z};
        const math::Quat mixed = math::normalize(math::slerp(from, to, keys.fraction));
        return {mixed.x, mixed.y, mixed.z, mixed.w};
    }
    return math::mix(first, second, keys.fraction);
}

} // namespace

Clip::Clip(asset::AnimationClipData data) noexcept
    : m_data(std::move(data))
{
}

core::Result<std::shared_ptr<const Clip>> Clip::create(asset::AnimationClipData data)
{
    if (core::Result<void> valid = validate(data); !valid)
    {
        return std::unexpected(valid.error());
    }
    return std::shared_ptr<const Clip>(new Clip(std::move(data)));
}

void Clip::sample(float time, std::span<JointPose> poses) const
{
    const float clamped = std::clamp(time, 0.0f, m_data.duration);
    for (JointPose& pose : poses)
    {
        pose = JointPose{};
    }
    for (const AnimationChannel& channel : m_data.channels)
    {
        if (channel.joint >= poses.size() || channel.times.empty())
        {
            continue;
        }
        JointPose& pose = poses[channel.joint];
        const math::Vec4 value = sampleChannel(channel, clamped);
        switch (channel.path)
        {
        case AnimationPath::Translation:
            pose.translation = math::Vec3(value);
            pose.hasTranslation = true;
            break;
        case AnimationPath::Rotation:
            pose.rotation = math::normalize(math::Quat{value.w, value.x, value.y, value.z});
            pose.hasRotation = true;
            break;
        case AnimationPath::Scale:
            pose.scale = math::Vec3(value);
            pose.hasScale = true;
            break;
        }
    }
}

void blendPoses(std::span<JointPose> target, std::span<const JointPose> other, float weight)
{
    const float amount = std::clamp(weight, 0.0f, 1.0f);
    for (std::size_t index = 0; index < target.size() && index < other.size(); ++index)
    {
        JointPose& into = target[index];
        const JointPose& from = other[index];
        if (from.hasTranslation)
        {
            into.translation = into.hasTranslation ? math::mix(into.translation, from.translation, amount)
                                                   : from.translation;
            into.hasTranslation = true;
        }
        if (from.hasRotation)
        {
            into.rotation = into.hasRotation ? math::normalize(math::slerp(into.rotation, from.rotation, amount))
                                             : from.rotation;
            into.hasRotation = true;
        }
        if (from.hasScale)
        {
            into.scale = into.hasScale ? math::mix(into.scale, from.scale, amount) : from.scale;
            into.hasScale = true;
        }
    }
}

} // namespace devex::animation
