#include <devex/asset/CurveData.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace devex::asset {

float CurveData::evaluate(float time) const noexcept
{
    if (keys.empty())
    {
        return time;
    }
    if (time <= keys.front().time)
    {
        return keys.front().value;
    }
    if (time >= keys.back().time)
    {
        return keys.back().value;
    }
    const auto next = std::ranges::upper_bound(keys, time, {}, &CurveKey::time);
    const CurveKey& right = *next;
    const CurveKey& left = *std::prev(next);
    const float span = right.time - left.time;
    if (span <= 0.0f)
    {
        return right.value;
    }
    const float t = (time - left.time) / span;
    const float t2 = t * t;
    const float t3 = t2 * t;
    // Cubic Hermite basis, the tangents scaled to the span of the segment.
    const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    const float h10 = t3 - 2.0f * t2 + t;
    const float h01 = -2.0f * t3 + 3.0f * t2;
    const float h11 = t3 - t2;
    return h00 * left.value + h10 * span * left.outTangent + h01 * right.value + h11 * span * right.inTangent;
}

core::Result<void> validate(const CurveData& curve)
{
    if (curve.keys.size() < 2)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "a curve needs two keys at least");
    }
    for (std::size_t index = 0; index < curve.keys.size(); ++index)
    {
        const CurveKey& key = curve.keys[index];
        if (!std::isfinite(key.time) || !std::isfinite(key.value) || !std::isfinite(key.inTangent) ||
            !std::isfinite(key.outTangent))
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "key {} of the curve is not a number", index);
        }
        if (index > 0 && key.time <= curve.keys[index - 1].time)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "the keys of the curve must go forward in time");
        }
    }
    return {};
}

CurveData linearCurve()
{
    return CurveData{.keys = {{.time = 0.0f, .value = 0.0f, .inTangent = 1.0f, .outTangent = 1.0f},
                              {.time = 1.0f, .value = 1.0f, .inTangent = 1.0f, .outTangent = 1.0f}}};
}

void smoothTangents(CurveData& curve, std::size_t key)
{
    if (key >= curve.keys.size())
    {
        return;
    }
    CurveKey& current = curve.keys[key];
    if (key == 0 || key + 1 == curve.keys.size())
    {
        current.inTangent = 0.0f;
        current.outTangent = 0.0f;
        return;
    }
    const CurveKey& previous = curve.keys[key - 1];
    const CurveKey& next = curve.keys[key + 1];
    const float span = next.time - previous.time;
    const float slope = span > 0.0f ? (next.value - previous.value) / span : 0.0f;
    current.inTangent = slope;
    current.outTangent = slope;
}

} // namespace devex::asset
