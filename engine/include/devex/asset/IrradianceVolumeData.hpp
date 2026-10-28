#pragma once

#include <devex/core/Export.hpp>

#include <devex/core/Error.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// Irradiance volumes: the light that arrives at the points of a grid in a box of a scene, baked by
// the editor, which lights the surfaces in the box in place of the sky.
namespace devex::asset {

// A bake of a volume of a scene: a binary file, the same bytes as its cooked asset.
inline constexpr std::string_view irradianceVolumeExtension = ".dvxirradiance";

// The coefficients each probe keeps: nine spherical harmonics of red, green and blue.
inline constexpr std::size_t irradianceCoefficients = 27;

// The light at the probes of a grid, which spans its box from corner to corner.
struct DEVEX_API IrradianceVolumeData
{
    // Probes along X, Y and Z, two at least along each.
    std::uint32_t countX = 2;
    std::uint32_t countY = 2;
    std::uint32_t countZ = 2;
    // For each probe, X first, then Y, then Z: the harmonics of the light a surface reflects divided by
    // pi, nine of red, green and blue, the red, green and blue of the first one first.
    std::vector<float> coefficients;

    [[nodiscard]] std::size_t probeCount() const noexcept
    {
        return std::size_t{countX} * countY * countZ;
    }

    bool operator==(const IrradianceVolumeData&) const = default;
};

// The most probes a volume may have.
inline constexpr std::size_t maxIrradianceProbes = 16384;

// Two probes at least along each axis, no more than the largest number in all, and their coefficients,
// all of them numbers.
[[nodiscard]] DEVEX_API core::Result<void> validate(const IrradianceVolumeData& volume);

} // namespace devex::asset
