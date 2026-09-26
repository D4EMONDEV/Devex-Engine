#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>

// How a tween moves from its start to its end: the classic easing curves, in, out and both. Each
// takes the progress of the tween from 0 to 1 and returns how far the value is, 0 at the start and
// 1 at the end; back and elastic go beyond on the way.
namespace devex::math {

enum class Ease : std::uint8_t
{
    Linear,
    InQuad,
    OutQuad,
    InOutQuad,
    InCubic,
    OutCubic,
    InOutCubic,
    InSine,
    OutSine,
    InOutSine,
    InExpo,
    OutExpo,
    InOutExpo,
    InBack,
    OutBack,
    InOutBack,
    InElastic,
    OutElastic,
    InOutElastic,
    InBounce,
    OutBounce,
    InOutBounce,
};

inline constexpr std::uint8_t easeCount = 22;

namespace detail {

[[nodiscard]] inline float outBounce(float t) noexcept
{
    constexpr float n = 7.5625f;
    constexpr float d = 2.75f;
    if (t < 1.0f / d)
    {
        return n * t * t;
    }
    if (t < 2.0f / d)
    {
        t -= 1.5f / d;
        return n * t * t + 0.75f;
    }
    if (t < 2.5f / d)
    {
        t -= 2.25f / d;
        return n * t * t + 0.9375f;
    }
    t -= 2.625f / d;
    return n * t * t + 0.984375f;
}

} // namespace detail

// The progress along the curve, for a progress in time from 0 to 1 (clamped).
[[nodiscard]] inline float ease(Ease curve, float t) noexcept
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    constexpr float pi = std::numbers::pi_v<float>;
    // How far back and elastic curves overshoot.
    constexpr float back = 1.70158f;
    constexpr float backInOut = back * 1.525f;
    constexpr float elastic = 2.0f * pi / 3.0f;
    constexpr float elasticInOut = 2.0f * pi / 4.5f;
    switch (curve)
    {
    case Ease::Linear:
        return t;
    case Ease::InQuad:
        return t * t;
    case Ease::OutQuad:
        return 1.0f - (1.0f - t) * (1.0f - t);
    case Ease::InOutQuad:
        return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
    case Ease::InCubic:
        return t * t * t;
    case Ease::OutCubic:
        return 1.0f - std::pow(1.0f - t, 3.0f);
    case Ease::InOutCubic:
        return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
    case Ease::InSine:
        return 1.0f - std::cos(t * pi / 2.0f);
    case Ease::OutSine:
        return std::sin(t * pi / 2.0f);
    case Ease::InOutSine:
        return -(std::cos(pi * t) - 1.0f) / 2.0f;
    case Ease::InExpo:
        return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
    case Ease::OutExpo:
        return t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
    case Ease::InOutExpo:
        if (t == 0.0f || t == 1.0f)
        {
            return t;
        }
        return t < 0.5f ? std::pow(2.0f, 20.0f * t - 10.0f) / 2.0f : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) / 2.0f;
    case Ease::InBack:
        return (back + 1.0f) * t * t * t - back * t * t;
    case Ease::OutBack:
        return 1.0f + (back + 1.0f) * std::pow(t - 1.0f, 3.0f) + back * std::pow(t - 1.0f, 2.0f);
    case Ease::InOutBack:
        return t < 0.5f ? (std::pow(2.0f * t, 2.0f) * ((backInOut + 1.0f) * 2.0f * t - backInOut)) / 2.0f
                        : (std::pow(2.0f * t - 2.0f, 2.0f) * ((backInOut + 1.0f) * (t * 2.0f - 2.0f) + backInOut) + 2.0f) / 2.0f;
    case Ease::InElastic:
        if (t == 0.0f || t == 1.0f)
        {
            return t;
        }
        return -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * elastic);
    case Ease::OutElastic:
        if (t == 0.0f || t == 1.0f)
        {
            return t;
        }
        return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * elastic) + 1.0f;
    case Ease::InOutElastic:
        if (t == 0.0f || t == 1.0f)
        {
            return t;
        }
        return t < 0.5f ? -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * elasticInOut)) / 2.0f
                        : (std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * elasticInOut)) / 2.0f + 1.0f;
    case Ease::InBounce:
        return 1.0f - detail::outBounce(1.0f - t);
    case Ease::OutBounce:
        return detail::outBounce(t);
    case Ease::InOutBounce:
        return t < 0.5f ? (1.0f - detail::outBounce(1.0f - 2.0f * t)) / 2.0f : (1.0f + detail::outBounce(2.0f * t - 1.0f)) / 2.0f;
    }
    return t;
}

} // namespace devex::math
