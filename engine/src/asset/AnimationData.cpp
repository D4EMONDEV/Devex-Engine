#include <devex/asset/AnimationData.hpp>

#include <algorithm>

namespace devex::asset {

std::size_t componentCount(AnimationPath path) noexcept
{
    return path == AnimationPath::Rotation ? 4 : 3;
}

std::string_view toString(AnimationPath path) noexcept
{
    switch (path)
    {
    case AnimationPath::Translation:
        return "translation";
    case AnimationPath::Rotation:
        return "rotation";
    case AnimationPath::Scale:
        return "scale";
    }
    return "translation";
}

std::string_view toString(AnimationInterpolation interpolation) noexcept
{
    switch (interpolation)
    {
    case AnimationInterpolation::Linear:
        return "linear";
    case AnimationInterpolation::Step:
        return "step";
    case AnimationInterpolation::CubicSpline:
        return "cubic spline";
    }
    return "linear";
}

core::Result<void> validate(const AnimationClipData& clip)
{
    if (clip.joints.empty() || clip.channels.empty())
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the clip animates nothing");
    }
    if (!(clip.duration > 0.0f))
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the clip lasts {} seconds",
                               clip.duration);
    }
    for (const AnimationChannel& channel : clip.channels)
    {
        if (channel.joint >= clip.joints.size())
        {
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "a channel drives joint {} of the {} joints of the clip",
                                   channel.joint, clip.joints.size());
        }
        if (channel.times.empty())
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "a channel of '{}' has no key",
                                   clip.joints[channel.joint]);
        }
        if (!std::ranges::is_sorted(channel.times))
        {
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "the keys of '{}' are not in order", clip.joints[channel.joint]);
        }
        const std::size_t perKey = componentCount(channel.path) *
                                   (channel.interpolation == AnimationInterpolation::CubicSpline ? 3 : 1);
        if (channel.values.size() != channel.times.size() * perKey)
        {
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "a {} channel of '{}' holds {} values for {} keys",
                                   toString(channel.path), clip.joints[channel.joint],
                                   channel.values.size(), channel.times.size());
        }
    }
    return {};
}

} // namespace devex::asset
