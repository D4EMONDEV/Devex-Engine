#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdint>

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

using glm::angleAxis;
using glm::clamp;
using glm::cross;
using glm::degrees;
using glm::dot;
using glm::inverse;
using glm::length;
using glm::mat4_cast;
using glm::mix;
using glm::normalize;
using glm::radians;
using glm::rotate;
using glm::scale;
using glm::translate;

// Size of a surface in whole units, such as a window or a framebuffer.
struct Extent2D
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    bool operator==(const Extent2D&) const = default;
};

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

} // namespace devex::math
