#pragma once

#include <devex/core/Export.hpp>

#include <devex/math/Math.hpp>

#include <optional>
#include <string>
#include <string_view>

// Colours as the interface draws them, which are linear, and as the eye and a colour picker see
// them, which are encoded in sRGB: the square of a picker spreads the encoded colour evenly.
namespace devex::ui {

[[nodiscard]] DEVEX_API float srgbFromLinear(float linear) noexcept;
[[nodiscard]] DEVEX_API float linearFromSrgb(float srgb) noexcept;
// The opacity stays as it is.
[[nodiscard]] DEVEX_API math::Vec4 srgbFromLinear(math::Vec4 linear) noexcept;
[[nodiscard]] DEVEX_API math::Vec4 linearFromSrgb(math::Vec4 srgb) noexcept;

// Hue, saturation and value from 0 to 1, of a colour whose channels go from 0 to 1.
[[nodiscard]] DEVEX_API math::Vec3 hsvFromRgb(math::Vec3 rgb) noexcept;
[[nodiscard]] DEVEX_API math::Vec3 rgbFromHsv(math::Vec3 hsv) noexcept;

// A colour encoded in sRGB as hexadecimal: RRGGBB, then AA when `alpha` asks for it.
[[nodiscard]] DEVEX_API std::string hexFromColor(math::Vec4 srgb, bool alpha);
// Reads RGB, RGBA, RRGGBB or RRGGBBAA, with or without a #; the opacity is 1 when not written.
[[nodiscard]] DEVEX_API std::optional<math::Vec4> colorFromHex(std::string_view text);

} // namespace devex::ui
