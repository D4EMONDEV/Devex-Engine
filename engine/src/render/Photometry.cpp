#include <devex/render/Photometry.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace devex::render {
namespace {

// Linear sRGB of the Planckian locus, from Kim et al.'s cubic approximation of its chromaticity.
[[nodiscard]] math::Vec3 planckianRgb(float kelvins) noexcept
{
    const double t = std::clamp(static_cast<double>(kelvins), 1667.0, 25000.0);
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double x = t <= 4000.0
                         ? -0.2661239e9 / t3 - 0.2343589e6 / t2 + 0.8776956e3 / t + 0.179910
                         : -3.0258469e9 / t3 + 2.1070379e6 / t2 + 0.2226347e3 / t + 0.240390;
    const double x2 = x * x;
    const double x3 = x2 * x;
    double y = 0.0;
    if (t <= 2222.0)
    {
        y = -1.1063814 * x3 - 1.34811020 * x2 + 2.18555832 * x - 0.20219683;
    }
    else if (t <= 4000.0)
    {
        y = -0.9549476 * x3 - 1.37418593 * x2 + 2.09137015 * x - 0.16748867;
    }
    else
    {
        y = 3.0817580 * x3 - 5.87338670 * x2 + 3.75112997 * x - 0.37001483;
    }

    const double capitalX = x / y;
    const double capitalZ = (1.0 - x - y) / y;
    const auto channel = [](double value) { return static_cast<float>(std::max(value, 0.0)); };
    return {channel(3.2404542 * capitalX - 1.5371385 - 0.4985314 * capitalZ),
            channel(-0.9692660 * capitalX + 1.8760108 + 0.0415560 * capitalZ),
            channel(0.0556434 * capitalX - 0.2040259 + 1.0572252 * capitalZ)};
}

} // namespace

float exposureFromEv100(float ev100) noexcept
{
    return 1.0f / (1.2f * std::pow(2.0f, ev100));
}

float ev100FromAverageLuminance(float luminance) noexcept
{
    // Solves luminance * exposureFromEv100(ev100) = 0.18.
    return std::log2(std::max(luminance, 1e-6f) / (1.2f * 0.18f));
}

float luminousIntensityFromPower(float lumens) noexcept
{
    return lumens / (4.0f * std::numbers::pi_v<float>);
}

math::Vec3 colorFromTemperature(float kelvins) noexcept
{
    static const math::Vec3 white = planckianRgb(6500.0f);
    const math::Vec3 color = planckianRgb(kelvins) / white;
    const float luminance = 0.2126f * color.r + 0.7152f * color.g + 0.0722f * color.b;
    return luminance > 0.0f ? color / luminance : math::Vec3{1.0f};
}

float averageLuminance(std::span<float> luminances, float lowFraction, float highFraction) noexcept
{
    if (luminances.empty())
    {
        return 0.0f;
    }
    std::ranges::sort(luminances);
    const std::size_t count = luminances.size();
    const auto first = std::min(static_cast<std::size_t>(static_cast<float>(count) * lowFraction), count - 1);
    const auto last = std::clamp(static_cast<std::size_t>(static_cast<float>(count) * highFraction),
                                 first + 1, count);

    // The geometric mean weighs a bright sky and a dark ground evenly, like the eye does.
    double logSum = 0.0;
    for (std::size_t index = first; index < last; ++index)
    {
        logSum += std::log2(std::max(static_cast<double>(luminances[index]), 1e-6));
    }
    return static_cast<float>(std::pow(2.0, logSum / static_cast<double>(last - first)));
}

float adaptExposure(float current, float target, float speed, float deltaSeconds) noexcept
{
    const float blend = 1.0f - std::exp(-std::max(speed, 0.0f) * std::max(deltaSeconds, 0.0f));
    return current + (target - current) * blend;
}

} // namespace devex::render
