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

} // namespace

math::Vec3 EditorCamera::position() const noexcept
{
    return m_pivot - forward() * m_distance;
}

math::Quat EditorCamera::rotation() const noexcept
{
    return math::angleAxis(math::radians(m_yaw), up) * math::angleAxis(math::radians(m_pitch), right);
}

math::Vec3 EditorCamera::forward() const noexcept
{
    return rotation() * math::Vec3{0.0f, 0.0f, -1.0f};
}

math::Vec3 EditorCamera::pivot() const noexcept
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
    m_pivot += rotation() * math::normalize(direction) * step;
}

void EditorCamera::orbit(math::Vec2 mouseDelta) noexcept
{
    m_yaw -= mouseDelta.x * orbitSensitivity;
    m_pitch = std::clamp(m_pitch - mouseDelta.y * orbitSensitivity, -89.0f, 89.0f);
}

void EditorCamera::pan(math::Vec2 mouseDelta, float viewportHeight) noexcept
{
    // One pixel covers this much at the pivot's distance.
    const float metersPerPixel = 2.0f * m_distance * std::tan(verticalFov * 0.5f) / std::max(viewportHeight, 1.0f);
    const math::Quat orientation = rotation();
    m_pivot += orientation * math::Vec3{-mouseDelta.x * metersPerPixel, mouseDelta.y * metersPerPixel, 0.0f};
}

void EditorCamera::dolly(float wheelSteps) noexcept
{
    m_distance = std::clamp(m_distance * std::pow(0.85f, wheelSteps), minimumDistance, maximumDistance);
}

void EditorCamera::changeSpeed(float wheelSteps) noexcept
{
    m_speed = std::clamp(m_speed * std::pow(1.2f, wheelSteps), 0.1f, 1000.0f);
}

void EditorCamera::frame(math::Vec3 center, float radius) noexcept
{
    m_pivot = center;
    const float fitted = std::max(radius, 0.1f) / std::sin(verticalFov * 0.5f);
    m_distance = std::clamp(fitted * 1.2f, minimumDistance, maximumDistance);
}

} // namespace devex::tools::detail
