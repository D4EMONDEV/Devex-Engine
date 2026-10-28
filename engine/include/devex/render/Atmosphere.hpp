#pragma once

#include <devex/core/Export.hpp>

#include <devex/math/Math.hpp>
#include <devex/render/RenderWorld.hpp>

// What the air of an atmosphere lets through, computed on the CPU as the GPU computes its table:
// the colour and the strength of the sun that reaches the scene.
namespace devex::render {

// The share of the light the air lets through from `altitude` kilometers above the ground towards a
// direction of that cosine with the zenith, for red, green and blue: nearly white straight up, red
// and faint along the horizon, nothing where the planet stands in the way.
[[nodiscard]] DEVEX_API math::Vec3 atmosphereTransmittance(const RenderAtmosphere& air, float altitude, float cosZenith) noexcept;

} // namespace devex::render
