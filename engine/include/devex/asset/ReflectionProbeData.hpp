#pragma once

#include <devex/core/Export.hpp>

#include <devex/core/Error.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Reflection probes: the light around a point of a scene, captured by the editor as a cube map,
// which the surfaces near the point reflect in place of the sky.
namespace devex::asset {

// A capture baked for a probe of a scene: a binary file, the same bytes as its cooked asset.
inline constexpr std::string_view reflectionProbeExtension = ".dvxprobe";

// The faces of a cube map of light, as captured.
struct DEVEX_API ReflectionProbeData
{
    // The width and the height of each face, in pixels.
    std::uint32_t size = 0;
    // The light of a pixel, in nits, is its value divided by the scale, which keeps a bright sky
    // within the range of half floats.
    float scale = 1.0f;
    // Six faces in the order of Vulkan cube maps (+X, -X, +Y, -Y, +Z, -Z), each of size × size
    // pixels from the top row down, of four half floats: red, green, blue and an unused one.
    std::vector<std::uint16_t> pixels;

    [[nodiscard]] std::size_t faceValues() const noexcept
    {
        return std::size_t{size} * size * 4;
    }

    bool operator==(const ReflectionProbeData&) const = default;
};

// The largest face a probe may have.
inline constexpr std::uint32_t maxReflectionProbeSize = 1024;

// Faces of a size from 1 to the largest, holding six of them.
[[nodiscard]] DEVEX_API core::Result<void> validate(const ReflectionProbeData& probe);

// Makes a probe of faces of light in nits, three floats a pixel, in the order of its pixels: the
// scale is chosen so that the brightest pixel fits the half floats.
[[nodiscard]] DEVEX_API ReflectionProbeData makeReflectionProbe(std::uint32_t size, std::span<const float> rgb);

// The light of a pixel of a face, in nits.
struct DEVEX_API ProbeTexel
{
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
};
[[nodiscard]] DEVEX_API ProbeTexel probeTexel(const ReflectionProbeData& probe, std::uint32_t face, std::uint32_t x,
                                              std::uint32_t y) noexcept;

} // namespace devex::asset
