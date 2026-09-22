#pragma once

#include <devex/math/Math.hpp>

#include <array>

// What a view can see: the six planes of its frustum, and the test that answers whether a box is
// worth drawing. Pure geometry, so it is tested without a GPU.
namespace devex::render {

// A plane as ax + by + cz + d = 0, whose normal points into the frustum.
using Plane = math::Vec4;

struct Frustum
{
    // Left, right, bottom, top, near, far.
    std::array<Plane, 6> planes{};

    // Whether any part of the box lies inside every plane. A box that only crosses a corner is
    // kept: the test is quick rather than exact.
    [[nodiscard]] bool intersects(const math::Aabb& box) const noexcept;
    // The four sides alone, without the near and far planes. A shadow cascade looks along its
    // own depth, so anything between its sides casts into it, however far away it stands.
    [[nodiscard]] bool intersectsSides(const math::Aabb& box) const noexcept;
};

// The frustum of a view and projection, read from the matrix itself. A plane that the projection
// leaves undefined, such as the far plane of an infinite projection, comes back empty and lets
// everything through.
[[nodiscard]] Frustum frustumOf(const math::Mat4& viewProjection) noexcept;

} // namespace devex::render
