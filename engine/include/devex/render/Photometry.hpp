#pragma once

#include <devex/math/Math.hpp>

#include <span>

// Conversions between the physical light units used by the engine: lux for directional light,
// lumens for local lights, nits for luminance, and EV100 for camera exposure.
namespace devex::render {

// The scale that maps scene luminance to the display for an exposure value at ISO 100, with the
// usual lens and sensor constants: 1 / (1.2 * 2^ev100).
[[nodiscard]] float exposureFromEv100(float ev100) noexcept;

// The EV100 at which an average luminance, in nits, is exposed to middle grey (18 %).
[[nodiscard]] float ev100FromAverageLuminance(float luminance) noexcept;

// Luminous intensity in candelas of a light emitting its power in every direction.
[[nodiscard]] float luminousIntensityFromPower(float lumens) noexcept;

// Linear RGB color of a black body at the temperature, from 1667 to 25000 kelvins, scaled so that
// 6500 K is white and the luminance is 1.
[[nodiscard]] math::Vec3 colorFromTemperature(float kelvins) noexcept;

// Average luminance of samples in nits, ignoring the darkest and brightest ones so that small
// highlights and deep shadows do not drive the exposure. The samples are reordered.
[[nodiscard]] float averageLuminance(std::span<float> luminances, float lowFraction = 0.1f,
                                     float highFraction = 0.9f) noexcept;

// Moves an exposure value towards a target, as fast as `speed` per second, framerate independent.
[[nodiscard]] float adaptExposure(float current, float target, float speed,
                                  float deltaSeconds) noexcept;

} // namespace devex::render
