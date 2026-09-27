#include "EditorCamera.hpp"

#include <algorithm>
#include <cmath>

namespace devex::tools::detail {
namespace {

constexpr float lookSensitivity = 0.15f; // degrees per pixel
constexpr float orbitSensitivity = 0.25f;
constexpr float minimumDistance = 0.05f;
constexpr float maximumDistance = 10000.0f;
constexpr math::Vec3 up{0.0f, 1.0f, 0.0f};
constexpr math::Vec3 right{1.0f, 0.0f, 0.0f};

constexpr float minimumOrthographicSize = 0.01f;
constexpr float maximumOrthographicSize = 100000.0f;

} // namespace

bool EditorCamera::isTwoD() const noexcept
{
    return m_twoD;
}

void EditorCamera::setTwoD(bool twoD) noexcept
{
    m_twoD = twoD;
}

float EditorCamera::orthographicSize() const noexcept
{
    return m_orthographicSize;
}

void EditorCamera::setOrthographicSize(float size) noexcept
{
    m_orthographicSize = std::clamp(size, minimumOrthographicSize, maximumOrthographicSize);
}

math::Vec2 EditorCamera::center() const noexcept
{
    return m_center;
}

void EditorCamera::setView2D(math::Vec2 center, float orthographicSize) noexcept
{
    m_center = center;
    setOrthographicSize(orthographicSize);
}

void EditorCamera::zoomAt(float wheelSteps, math::Vec2 pixel, math::Vec2 viewportSize) noexcept
{
    const float height = std::max(viewportSize.y, 1.0f);
    const auto worldAt = [&](float size) {
        const float metersPerPixel = 2.0f * size / height;
        return math::Vec2{(pixel.x - viewportSize.x * 0.5f) * metersPerPixel, (viewportSize.y * 0.5f - pixel.y) * metersPerPixel};
    };
    const math::Vec2 before = worldAt(m_orthographicSize);
    setOrthographicSize(m_orthographicSize * std::pow(0.85f, wheelSteps));
    const math::Vec2 after = worldAt(m_orthographicSize);
    m_center += before - after;
}

math::Vec3 EditorCamera::position() const noexcept
{
    return pivot() - forward() * (m_twoD ? twoDDistance : m_distance);
}

math::Quat EditorCamera::rotation() const noexcept
{
    if (m_twoD)
    {
        return math::Quat{1.0f, 0.0f, 0.0f, 0.0f};
    }
    return math::angleAxis(math::radians(m_yaw), up) * math::angleAxis(math::radians(m_pitch), right);
}

math::Vec3 EditorCamera::forward() const noexcept
{
    return rotation() * math::Vec3{0.0f, 0.0f, -1.0f};
}

math::Vec3 EditorCamera::pivot() const noexcept
{
    return m_twoD ? math::Vec3(m_center, 0.0f) : m_pivot;
}

math::Vec3 EditorCamera::pivot3D() const noexcept
{
    return m_pivot;
}

math::Mat4 EditorCamera::view() const noexcept
{
    return math::inverse(math::composeTrs({position(), rotation(), math::Vec3{1.0f}}));
}

float EditorCamera::speed() const noexcept
{
    return m_speed;
}

float EditorCamera::yaw() const noexcept
{
    return m_yaw;
}

float EditorCamera::pitch() const noexcept
{
    return m_pitch;
}

float EditorCamera::distance() const noexcept
{
    return m_distance;
}

void EditorCamera::lookAt(math::Vec3 position, math::Vec3 target) noexcept
{
    const math::Vec3 offset = target - position;
    m_distance = std::clamp(math::length(offset), minimumDistance, maximumDistance);
    const math::Vec3 direction = math::length(offset) > 0.0f ? offset / math::length(offset) : math::Vec3{0.0f, 0.0f, -1.0f};
    m_yaw = math::degrees(std::atan2(-direction.x, -direction.z));
    m_pitch = math::degrees(std::asin(std::clamp(direction.y, -1.0f, 1.0f)));
    m_pivot = position + direction * m_distance;
}

void EditorCamera::set(math::Vec3 pivot, float yaw, float pitch, float distance, float speed) noexcept
{
    m_pivot = pivot;
    m_yaw = yaw;
    m_pitch = std::clamp(pitch, -89.0f, 89.0f);
    m_distance = std::clamp(distance, minimumDistance, maximumDistance);
    m_speed = std::clamp(speed, 0.1f, 1000.0f);
}

void EditorCamera::look(math::Vec2 mouseDelta) noexcept
{
    if (m_twoD)
    {
        return;
    }
    // The camera turns in place: the pivot moves with it.
    const math::Vec3 eye = position();
    m_yaw -= mouseDelta.x * lookSensitivity;
    m_pitch = std::clamp(m_pitch - mouseDelta.y * lookSensitivity, -89.0f, 89.0f);
    m_pivot = eye + forward() * m_distance;
}

void EditorCamera::fly(math::Vec3 direction, float seconds, bool fast) noexcept
{
    if (math::length(direction) <= 0.0f)
    {
        return;
    }
    const float step = m_speed * (fast ? 4.0f : 1.0f) * seconds;
    // In 2D, the keys slide the view across the plane.
    if (m_twoD)
    {
        const math::Vec2 flat{direction.x, direction.y};
        if (math::length(flat) > 0.0f)
        {
            m_center += math::normalize(flat) * step;
        }
        return;
    }
    m_pivot += rotation() * math::normalize(direction) * step;
}

void EditorCamera::orbit(math::Vec2 mouseDelta) noexcept
{
    if (m_twoD)
    {
        return;
    }
    m_yaw -= mouseDelta.x * orbitSensitivity;
    m_pitch = std::clamp(m_pitch - mouseDelta.y * orbitSensitivity, -89.0f, 89.0f);
}

void EditorCamera::pan(math::Vec2 mouseDelta, float viewportHeight) noexcept
{
    // One pixel covers this much at the pivot's distance.
    const float metersPerPixel = 2.0f * (m_twoD ? m_orthographicSize : m_distance * std::tan(verticalFov * 0.5f)) /
                                 std::max(viewportHeight, 1.0f);
    if (m_twoD)
    {
        m_center += math::Vec2{-mouseDelta.x * metersPerPixel, mouseDelta.y * metersPerPixel};
        return;
    }
    const math::Quat orientation = rotation();
    m_pivot += orientation * math::Vec3{-mouseDelta.x * metersPerPixel, mouseDelta.y * metersPerPixel, 0.0f};
}

void EditorCamera::dolly(float wheelSteps) noexcept
{
    if (m_twoD)
    {
        setOrthographicSize(m_orthographicSize * std::pow(0.85f, wheelSteps));
        return;
    }
    m_distance = std::clamp(m_distance * std::pow(0.85f, wheelSteps), minimumDistance, maximumDistance);
}

void EditorCamera::changeSpeed(float wheelSteps) noexcept
{
    m_speed = std::clamp(m_speed * std::pow(1.2f, wheelSteps), 0.1f, 1000.0f);
}

void EditorCamera::frame(math::Vec3 center, float radius) noexcept
{
    if (m_twoD)
    {
        m_center = math::Vec2{center.x, center.y};
        setOrthographicSize(std::max(radius, 0.1f) * 1.2f);
        return;
    }
    m_pivot = center;
    const float fitted = std::max(radius, 0.1f) / std::sin(verticalFov * 0.5f);
    m_distance = std::clamp(fitted * 1.2f, minimumDistance, maximumDistance);
}

} // namespace devex::tools::detail
