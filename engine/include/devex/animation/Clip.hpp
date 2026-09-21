#pragma once

#include <devex/asset/AnimationData.hpp>
#include <devex/core/Error.hpp>
#include <devex/math/Math.hpp>

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace devex::animation {

// The local transform a clip gives one joint at one time. A property the clip does not drive keeps
// whatever the bone already has.
struct JointPose
{
    math::Vec3 translation{0.0f};
    math::Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    math::Vec3 scale{1.0f};
    bool hasTranslation = false;
    bool hasRotation = false;
    bool hasScale = false;
};

// An animation ready to play, shared by every animator that plays it.
class Clip
{
public:
    [[nodiscard]] static core::Result<std::shared_ptr<const Clip>> create(asset::AnimationClipData data);

    [[nodiscard]] const asset::AnimationClipData& data() const noexcept
    {
        return m_data;
    }

    // Seconds.
    [[nodiscard]] float duration() const noexcept
    {
        return m_data.duration;
    }

    // The names of the joints the clip drives, in the order sample() writes them.
    [[nodiscard]] std::span<const std::string> joints() const noexcept
    {
        return m_data.joints;
    }

    // Reads every joint at a time in seconds, clamped to the clip. poses holds one entry per joint.
    void sample(float time, std::span<JointPose> poses) const;

private:
    explicit Clip(asset::AnimationClipData data) noexcept;

    asset::AnimationClipData m_data;
};

// Mixes other into target, with a weight of 0 keeping target and 1 taking other. Rotations follow
// the shortest arc. A property only one side drives is taken from that side.
void blendPoses(std::span<JointPose> target, std::span<const JointPose> other, float weight);

} // namespace devex::animation
