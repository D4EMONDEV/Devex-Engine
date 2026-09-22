#include <devex/render/Culling.hpp>

#include <cmath>

namespace devex::render {
namespace {

// A plane of the matrix, normalized so that distances are in world units. A row that cancels out,
// as the far row of an infinite projection does, gives an empty plane.
[[nodiscard]] Plane normalized(Plane plane) noexcept
{
    const float length = std::sqrt(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
    return length > 1e-6f ? plane / length : Plane{0.0f};
}

// Whether the box lies entirely on the outer side of the plane. The corner tested is the one the
// normal points at, which is the last to leave the plane.
[[nodiscard]] bool isOutside(const Plane& plane, const math::Aabb& box) noexcept
{
    if (plane == Plane{0.0f})
    {
        return false;
    }
    const math::Vec3 nearest{plane.x >= 0.0f ? box.max.x : box.min.x,
                             plane.y >= 0.0f ? box.max.y : box.min.y,
                             plane.z >= 0.0f ? box.max.z : box.min.z};
    return plane.x * nearest.x + plane.y * nearest.y + plane.z * nearest.z + plane.w < 0.0f;
}

} // namespace

bool Frustum::intersects(const math::Aabb& box) const noexcept
{
    if (box.isEmpty())
    {
        return true;
    }
    for (const Plane& plane : planes)
    {
        if (isOutside(plane, box))
        {
            return false;
        }
    }
    return true;
}

bool Frustum::intersectsSides(const math::Aabb& box) const noexcept
{
    if (box.isEmpty())
    {
        return true;
    }
    for (std::size_t index = 0; index < 4; ++index)
    {
        if (isOutside(planes[index], box))
        {
            return false;
        }
    }
    return true;
}

Frustum frustumOf(const math::Mat4& viewProjection) noexcept
{
    // The rows of the matrix, which the columns of a column-major matrix hold across.
    const auto row = [&viewProjection](int index) {
        return Plane{viewProjection[0][index], viewProjection[1][index], viewProjection[2][index],
                     viewProjection[3][index]};
    };
    const Plane w = row(3);
    const Plane x = row(0);
    const Plane y = row(1);
    const Plane z = row(2);

    Frustum frustum;
    frustum.planes[0] = normalized(w + x);
    frustum.planes[1] = normalized(w - x);
    frustum.planes[2] = normalized(w + y);
    frustum.planes[3] = normalized(w - y);
    // Vulkan clips depth between 0 and w; with reversed depth the near plane is the far row.
    frustum.planes[4] = normalized(w - z);
    frustum.planes[5] = normalized(z);
    return frustum;
}

} // namespace devex::render
