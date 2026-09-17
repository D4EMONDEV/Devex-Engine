#include "EditorView.hpp"

#include <algorithm>
#include <cmath>

namespace devex::tools::detail {

math::Vec3 ViewportView::cameraPosition() const noexcept
{
    return math::Vec3(math::inverse(view)[3]);
}

math::Vec3 ViewportView::cameraForward() const noexcept
{
    // The third row of the view rotation is the camera's +Z axis in world space.
    const math::Vec3 backward{view[0][2], view[1][2], view[2][2]};
    return -math::normalize(backward);
}

std::optional<math::Vec2> ViewportView::project(math::Vec3 position) const noexcept
{
    const math::Vec4 viewPosition = view * math::Vec4(position, 1.0f);
    const float depth = -viewPosition.z;
    if (depth <= 1e-4f)
    {
        return std::nullopt;
    }
    const float tangent = std::tan(verticalFov * 0.5f);
    const float aspect = size.x / size.y;
    const float ndcX = viewPosition.x / (depth * tangent * aspect);
    const float ndcY = viewPosition.y / (depth * tangent);
    return math::Vec2{(ndcX + 1.0f) * 0.5f * size.x, (1.0f - ndcY) * 0.5f * size.y};
}

Ray ViewportView::ray(math::Vec2 pixel) const noexcept
{
    const float tangent = std::tan(verticalFov * 0.5f);
    const float aspect = size.x / size.y;
    const float ndcX = pixel.x / size.x * 2.0f - 1.0f;
    const float ndcY = 1.0f - pixel.y / size.y * 2.0f;
    const math::Mat4 cameraWorld = math::inverse(view);
    const math::Vec3 direction = math::Mat3(cameraWorld) * math::Vec3{ndcX * tangent * aspect, ndcY * tangent, -1.0f};
    return {math::Vec3(cameraWorld[3]), math::normalize(direction)};
}

float ViewportView::worldSize(math::Vec3 position, float pixels) const noexcept
{
    const float depth = std::max(-(view * math::Vec4(position, 1.0f)).z, 1e-3f);
    return depth * 2.0f * std::tan(verticalFov * 0.5f) * pixels / size.y;
}

std::optional<float> closestParameterOnLine(math::Vec3 origin, math::Vec3 axis, const Ray& ray) noexcept
{
    const math::Vec3 offset = origin - ray.origin;
    const float alignment = math::dot(axis, ray.direction);
    const float denominator = 1.0f - alignment * alignment;
    if (denominator < 1e-6f)
    {
        return std::nullopt;
    }
    return (alignment * math::dot(ray.direction, offset) - math::dot(axis, offset)) / denominator;
}

std::optional<math::Vec3> intersectPlane(const Ray& ray, math::Vec3 point, math::Vec3 normal) noexcept
{
    const float facing = math::dot(ray.direction, normal);
    if (std::abs(facing) < 1e-5f)
    {
        return std::nullopt;
    }
    const float distance = math::dot(point - ray.origin, normal) / facing;
    if (distance < 0.0f)
    {
        return std::nullopt;
    }
    return ray.origin + ray.direction * distance;
}

float distanceToSegment(math::Vec2 point, math::Vec2 start, math::Vec2 end) noexcept
{
    const math::Vec2 segment = end - start;
    const float lengthSquared = math::dot(segment, segment);
    const float t = lengthSquared > 0.0f ? std::clamp(math::dot(point - start, segment) / lengthSquared, 0.0f, 1.0f)
                                         : 0.0f;
    return math::length(point - (start + segment * t));
}

} // namespace devex::tools::detail
