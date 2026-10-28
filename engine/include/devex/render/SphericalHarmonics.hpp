#pragma once

#include <devex/core/Export.hpp>

#include <devex/math/Math.hpp>

#include <array>
#include <cstdint>
#include <span>

// The light arriving at a point, as spherical harmonics of the second order: what the irradiance
// volumes keep for each of their probes, as Ramamoorthi and Hanrahan's irradiance environment maps.
namespace devex::render {

// Nine coefficients of red, green and blue.
using IrradianceHarmonics = std::array<math::Vec3, 9>;

// From the light of a cube map in nits, three floats a pixel, six faces of size × size pixels from the
// top row down in the order of cube maps: the light a surface facing each direction reflects back,
// divided by pi, as the irradiance of the sky is kept. A white surface under a sky of 1 nit all
// around reflects 1.
[[nodiscard]] DEVEX_API IrradianceHarmonics projectIrradiance(std::span<const float> faces, std::uint32_t size) noexcept;

// What the harmonics give a surface facing a direction, which need not be normalized.
[[nodiscard]] DEVEX_API math::Vec3 evaluateIrradiance(const IrradianceHarmonics& harmonics, math::Vec3 normal) noexcept;

} // namespace devex::render
