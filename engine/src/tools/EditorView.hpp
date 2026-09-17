#pragma once

#include <devex/math/Math.hpp>

#include <optional>

namespace devex::tools::detail {

struct Ray
{
    math::Vec3 origin{0.0f};
    // Normalized.
    math::Vec3 direction{0.0f, 0.0f, -1.0f};
};

// How the viewport shows the world: a perspective camera and the size of the image in pixels. Pixels
// count from the top-left corner, like mouse positions.
struct ViewportView
{
    // World to view transform of a camera looking along -Z with +Y up.
    math::Mat4 view{1.0f};
    float verticalFov = math::radians(60.0f);
    math::Vec2 size{1.0f};

    [[nodiscard]] math::Vec3 cameraPosition() const noexcept;
    // The direction the camera looks at, normalized.
    [[nodiscard]] math::Vec3 cameraForward() const noexcept;
    // The pixel showing a world position, or nothing for a position behind the camera.
    [[nodiscard]] std::optional<math::Vec2> project(math::Vec3 position) const noexcept;
    // The ray from the camera through the center of a pixel.
    [[nodiscard]] Ray ray(math::Vec2 pixel) const noexcept;
    // The world length covering that many pixels at the distance of a position.
    [[nodiscard]] float worldSize(math::Vec3 position, float pixels) const noexcept;
};

// Parameter along the line origin + t * axis of its point closest to the ray, or nothing when the
// ray is parallel to the line. The axis must be normalized.
[[nodiscard]] std::optional<float> closestParameterOnLine(math::Vec3 origin, math::Vec3 axis, const Ray& ray) noexcept;

// Where the ray meets the plane through `point` with `normal`, or nothing when the ray runs along
// the plane or away from it.
[[nodiscard]] std::optional<math::Vec3> intersectPlane(const Ray& ray, math::Vec3 point, math::Vec3 normal) noexcept;

// Distance from a point to a segment, in 2D.
[[nodiscard]] float distanceToSegment(math::Vec2 point, math::Vec2 start, math::Vec2 end) noexcept;

} // namespace devex::tools::detail
