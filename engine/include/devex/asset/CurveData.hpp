#pragma once

#include <devex/core/Error.hpp>

#include <string_view>
#include <vector>

// A curve drawn by hand, which eases a tween between its start and its end: time goes from 0 to 1,
// and the value is 0 at the start and 1 at the end of a tween, below or beyond them in between for
// an overshoot or a bounce.
namespace devex::asset {

inline constexpr std::string_view curveExtension = ".dvxcurve";

// A point of the curve, with the slopes the curve leaves it with on each side.
struct CurveKey
{
    float time = 0.0f;
    float value = 0.0f;
    // Slopes in value per unit of time, arriving at the key and leaving it.
    float inTangent = 0.0f;
    float outTangent = 0.0f;

    bool operator==(const CurveKey&) const = default;
};

struct CurveData
{
    // Sorted by time, two at least.
    std::vector<CurveKey> keys;

    // The value at a time, the first or last value outside the keys. Between two keys, a cubic
    // Hermite spline follows their values and their tangents.
    [[nodiscard]] float evaluate(float time) const noexcept;

    bool operator==(const CurveData&) const = default;
};

// Keys in increasing time, finite values, at least two keys.
[[nodiscard]] core::Result<void> validate(const CurveData& curve);

// A straight line from (0, 0) to (1, 1).
[[nodiscard]] CurveData linearCurve();
// The tangents of a key that make the curve pass through it smoothly, as the slope from its
// neighbours; zero at the ends.
void smoothTangents(CurveData& curve, std::size_t key);

} // namespace devex::asset
