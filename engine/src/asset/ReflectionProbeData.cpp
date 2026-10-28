#include <devex/asset/ReflectionProbeData.hpp>

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>

namespace devex::asset {
namespace {

// The largest half float, a little under 65504, so that rounding never reaches infinity.
constexpr float largestHalf = 65000.0f;

} // namespace

core::Result<void> validate(const ReflectionProbeData& probe)
{
    if (probe.size == 0 || probe.size > maxReflectionProbeSize)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "a reflection probe has faces of {} pixels", probe.size);
    }
    if (probe.pixels.size() != probe.faceValues() * 6)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "a reflection probe of {} pixels has {} values", probe.size,
                               probe.pixels.size());
    }
    if (!(probe.scale > 0.0f) || !std::isfinite(probe.scale))
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "a reflection probe has no valid scale");
    }
    return {};
}

ReflectionProbeData makeReflectionProbe(std::uint32_t size, std::span<const float> rgb)
{
    ReflectionProbeData probe{.size = size};
    float brightest = 0.0f;
    for (const float value : rgb)
    {
        brightest = std::isfinite(value) ? std::max(brightest, value) : brightest;
    }
    // A power of two keeps the values exact when they are scaled back.
    probe.scale = brightest > largestHalf ? std::exp2(std::floor(std::log2(largestHalf / brightest))) : 1.0f;
    probe.pixels.resize(probe.faceValues() * 6);
    for (std::size_t pixel = 0; pixel * 3 + 2 < rgb.size() && pixel * 4 + 3 < probe.pixels.size(); ++pixel)
    {
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            const float value = rgb[pixel * 3 + channel];
            const float scaled = std::isfinite(value) ? std::clamp(value * probe.scale, 0.0f, largestHalf) : 0.0f;
            probe.pixels[pixel * 4 + channel] = glm::packHalf1x16(scaled);
        }
        probe.pixels[pixel * 4 + 3] = glm::packHalf1x16(1.0f);
    }
    return probe;
}

ProbeTexel probeTexel(const ReflectionProbeData& probe, std::uint32_t face, std::uint32_t x, std::uint32_t y) noexcept
{
    const std::size_t index = face * probe.faceValues() + (std::size_t{y} * probe.size + x) * 4;
    if (face >= 6 || x >= probe.size || y >= probe.size || index + 2 >= probe.pixels.size())
    {
        return {};
    }
    return {glm::unpackHalf1x16(probe.pixels[index]) / probe.scale, glm::unpackHalf1x16(probe.pixels[index + 1]) / probe.scale,
            glm::unpackHalf1x16(probe.pixels[index + 2]) / probe.scale};
}

} // namespace devex::asset
