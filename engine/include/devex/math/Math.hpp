#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>

// Conventions: Y-up, right-handed, -Z forward, meters, radians, depth in [0, 1].
namespace devex::math {

using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using IVec2 = glm::ivec2;
using UVec2 = glm::uvec2;
using Mat3 = glm::mat3;
using Mat4 = glm::mat4;
using Quat = glm::quat;

using glm::clamp;
using glm::cross;
using glm::degrees;
using glm::dot;
using glm::length;
using glm::normalize;
using glm::radians;

// Size of a surface in whole units, such as a window or a framebuffer.
struct Extent2D
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    bool operator==(const Extent2D&) const = default;
};

} // namespace devex::math
