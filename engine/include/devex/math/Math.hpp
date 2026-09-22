#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdint>
#include <limits>

// Conventions: Y-up, right-handed, -Z forward, meters, radians, counter-clockwise front faces.
namespace devex::math {

using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using IVec2 = glm::ivec2;
using UVec2 = glm::uvec2;
using Mat3 = glm::mat3;
using Mat4 = glm::mat4;
using Quat = glm::quat;

using glm::abs;
using glm::angleAxis;
using glm::clamp;
using glm::conjugate;
using glm::cross;
using glm::degrees;
using glm::dot;
using glm::eulerAngles;
using glm::inverse;
using glm::length;
using glm::lookAt;
using glm::max;
using glm::min;
using glm::mat4_cast;
using glm::mix;
using glm::normalize;
using glm::ortho;
using glm::perspective;
using glm::radians;
using glm::rotate;
using glm::scale;
using glm::slerp;
using glm::translate;

// Size of a surface in whole units, such as a window or a framebuffer.
struct Extent2D
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    bool operator==(const Extent2D&) const = default;
};

// Builds a rotation from angles around X, Y and Z in radians, the inverse of eulerAngles.
[[nodiscard]] inline Quat quatFromEulerAngles(Vec3 angles) noexcept
{
    return Quat(angles);
}

// Translation, rotation and scale, applied in the reverse order to a point.
struct Trs
{
    Vec3 translation{0.0f};
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    Vec3 scale{1.0f};
};

[[nodiscard]] inline Mat4 composeTrs(const Trs& trs) noexcept
{
    return glm::scale(glm::translate(Mat4{1.0f}, trs.translation) * glm::mat4_cast(trs.rotation),
                      trs.scale);
}

// Splits a matrix built from translation, rotation and scale. Shear is lost, and a mirroring
// transform is expressed as a negative X scale.
[[nodiscard]] inline Trs decomposeTrs(const Mat4& matrix) noexcept
{
    Trs trs;
    trs.translation = Vec3(matrix[3]);

    Vec3 axisX = Vec3(matrix[0]);
    const Vec3 axisY = Vec3(matrix[1]);
    const Vec3 axisZ = Vec3(matrix[2]);
    trs.scale = {glm::length(axisX), glm::length(axisY), glm::length(axisZ)};
    if (glm::dot(glm::cross(axisX, axisY), axisZ) < 0.0f)
    {
        trs.scale.x = -trs.scale.x;
        axisX = -axisX;
    }

    if (trs.scale.x != 0.0f && trs.scale.y != 0.0f && trs.scale.z != 0.0f)
    {
        const Mat3 rotation{axisX / std::abs(trs.scale.x), axisY / trs.scale.y, axisZ / trs.scale.z};
        trs.rotation = glm::normalize(glm::quat_cast(rotation));
    }
    return trs;
}

// Right-handed perspective projection with reversed depth and no far plane: depth is 1 at the
// near plane and tends to 0 at infinity, which keeps precision even for distant geometry. Clip
// space Y points up; graphics backends apply their own axis corrections.
[[nodiscard]] inline Mat4 perspectiveReverseZ(float verticalFov, float aspectRatio,
                                              float nearPlane) noexcept
{
    const float focalLength = 1.0f / std::tan(verticalFov * 0.5f);
    Mat4 projection{0.0f};
    projection[0][0] = focalLength / aspectRatio;
    projection[1][1] = focalLength;
    projection[2][3] = -1.0f;
    projection[3][2] = nearPlane;
    return projection;
}

// A box aligned with the axes, holding everything between its two corners. An empty box has its
// minimum above its maximum, which no point can satisfy.
struct Aabb
{
    Vec3 min{std::numeric_limits<float>::max()};
    Vec3 max{std::numeric_limits<float>::lowest()};

    [[nodiscard]] constexpr bool isEmpty() const noexcept
    {
        return min.x > max.x || min.y > max.y || min.z > max.z;
    }

    [[nodiscard]] constexpr Vec3 centre() const noexcept
    {
        return (min + max) * 0.5f;
    }

    [[nodiscard]] constexpr Vec3 extent() const noexcept
    {
        return (max - min) * 0.5f;
    }

    constexpr void add(Vec3 point) noexcept
    {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }

    // Grows the box by a fraction of its own size, for what moves inside it.
    [[nodiscard]] constexpr Aabb grown(float fraction) const noexcept
    {
        const Vec3 margin = extent() * fraction;
        return isEmpty() ? *this : Aabb{min - margin, max + margin};
    }
};

// The box that holds the transformed box: the eight corners, moved and enclosed again.
[[nodiscard]] inline Aabb transform(const Mat4& matrix, const Aabb& box) noexcept
{
    if (box.isEmpty())
    {
        return box;
    }
    Aabb result;
    for (int corner = 0; corner < 8; ++corner)
    {
        const Vec3 point{(corner & 1) != 0 ? box.max.x : box.min.x,
                         (corner & 2) != 0 ? box.max.y : box.min.y,
                         (corner & 4) != 0 ? box.max.z : box.min.z};
        result.add(Vec3(matrix * Vec4(point, 1.0f)));
    }
    return result;
}

} // namespace devex::math
