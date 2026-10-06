#pragma once

#include <devex/core/Export.hpp>

#include <devex/math/Math.hpp>
#include <devex/render/RenderWorld.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <utility>

// The shadows of the 2D lights, as Godot draws them: each shadowed light keeps a line of distances
// to the nearest occluder edge, one per direction around a point light, or one per strip along the
// light of a directional one. A pixel is in shadow when it lies beyond that distance.
namespace devex::render {

// How many distances a shadowed 2D light keeps.
inline constexpr std::uint32_t shadow2DResolution = 1024;
// The distance kept where no edge stands in the way.
inline constexpr float noOccluder2D = 1.0e9f;

// Distance i looks along the angle 2 pi (i + 0.5) / count from +X, counterclockwise: from the center to
// the nearest edge whose mask shares a bit with `mask`, within `radius`. Edges through the center are
// left out.
DEVEX_API void pointShadow2D(math::Vec2 center, float radius, std::span<const RenderOccluder2D> edges, std::uint32_t mask,
                             std::span<float> distances);

// The strips of a directional light: strip i starts on the line through `origin` across the light, at
// (i + 0.5) / count of `width` from its end at -width / 2, and goes `length` along the light.
struct DEVEX_API DirectionalShadowFrame2D
{
    math::Vec2 origin{0.0f};
    // The way the light goes, and the line across it, both of length 1.
    math::Vec2 direction{0.0f, -1.0f};
    math::Vec2 across{1.0f, 0.0f};
    float width = 1.0f;
    float length = 1.0f;
};

// The strips that cover a rectangle of the plane, starting `reach` upwind of it so that the occluders
// there cast their shadows into it.
[[nodiscard]] DEVEX_API DirectionalShadowFrame2D directionalShadowFrame(math::Vec2 direction, math::Vec2 low, math::Vec2 high,
                                                                        float reach) noexcept;
// Distance i is from the start of strip i to the nearest edge whose mask shares a bit with `mask`.
DEVEX_API void directionalShadow2D(const DirectionalShadowFrame2D& frame, std::span<const RenderOccluder2D> edges,
                                   std::uint32_t mask, std::span<float> distances);

// The rectangle of the plane z = 0 a camera sees, its lowest corner then its highest: the corners of
// the screen cast onto it. Nothing when the camera looks along the plane.
[[nodiscard]] DEVEX_API std::optional<std::pair<math::Vec2, math::Vec2>> visiblePlane2D(const math::Mat4& viewProjection) noexcept;

} // namespace devex::render
