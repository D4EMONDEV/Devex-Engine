#pragma once

#include <devex/math/Math.hpp>

namespace devex::tools::detail {

// The camera of the editor's viewport, independent of the cameras of the scene. It looks at a
// pivot in front of it: orbiting turns around the pivot, flying moves both, and framing an object
// moves the pivot onto it.
class EditorCamera
{
public:
    static constexpr float verticalFov = math::radians(60.0f);
    static constexpr float nearPlane = 0.05f;

    [[nodiscard]] math::Vec3 position() const noexcept;
    [[nodiscard]] math::Quat rotation() const noexcept;
    [[nodiscard]] math::Vec3 forward() const noexcept;
    [[nodiscard]] math::Vec3 pivot() const noexcept;
    // World to view transform.
    [[nodiscard]] math::Mat4 view() const noexcept;
    // Movement speed while flying, in meters per second.
    [[nodiscard]] float speed() const noexcept;
    // Yaw and pitch in degrees.
    [[nodiscard]] float yaw() const noexcept;
    [[nodiscard]] float pitch() const noexcept;
    [[nodiscard]] float distance() const noexcept;

    // Places the camera at a position, looking at a target.
    void lookAt(math::Vec3 position, math::Vec3 target) noexcept;
    // Restores a saved state.
    void set(math::Vec3 pivot, float yaw, float pitch, float distance, float speed) noexcept;

    // Turns in place by a mouse movement in pixels.
    void look(math::Vec2 mouseDelta) noexcept;
    // Moves by a direction in camera space (x right, y up, z backward) for a duration.
    void fly(math::Vec3 direction, float seconds, bool fast) noexcept;
    // Turns around the pivot by a mouse movement in pixels.
    void orbit(math::Vec2 mouseDelta) noexcept;
    // Slides the camera and the pivot so that the pivot follows the mouse.
    void pan(math::Vec2 mouseDelta, float viewportHeight) noexcept;
    // Moves towards the pivot for positive steps of the mouse wheel.
    void dolly(float wheelSteps) noexcept;
    // Changes the flying speed by steps of the mouse wheel.
    void changeSpeed(float wheelSteps) noexcept;
    // Moves the pivot to a sphere and backs away until the sphere fits the view.
    void frame(math::Vec3 center, float radius) noexcept;

private:
    math::Vec3 m_pivot{0.0f};
    float m_yaw = -30.0f;
    float m_pitch = -25.0f;
    float m_distance = 10.0f;
    float m_speed = 6.0f;
};

} // namespace devex::tools::detail
