#pragma once

#include <devex/math/Math.hpp>

namespace devex::tools::detail {

// The camera of the editor's viewport, independent of the cameras of the scene. It holds two views,
// the one of the 3D screen and the one of the 2D screen, and shows one at a time. In 3D, it looks
// at a pivot in front of it: orbiting turns around the pivot, flying moves both, and framing an
// object moves the pivot onto it. In 2D, it looks straight down -Z at the XY plane through an
// orthographic projection: it slides and zooms, but does not turn.
class EditorCamera
{
public:
    static constexpr float verticalFov = math::radians(60.0f);
    static constexpr float nearPlane = 0.05f;
    // How far in front of the XY plane the 2D camera stands, and how deep it sees.
    static constexpr float twoDDistance = 500.0f;
    static constexpr float twoDFarPlane = 1000.0f;

    [[nodiscard]] bool isTwoD() const noexcept;
    // Shows the 2D view or the 3D view; each stays where it was left.
    void setTwoD(bool twoD) noexcept;
    // Half the height the 2D view shows, in meters.
    [[nodiscard]] float orthographicSize() const noexcept;
    void setOrthographicSize(float size) noexcept;
    // The point of the XY plane at the middle of the 2D view.
    [[nodiscard]] math::Vec2 center() const noexcept;
    // Places the 2D view, whichever view is shown.
    void setView2D(math::Vec2 center, float orthographicSize) noexcept;
    // Zooms the 2D view by steps of the mouse wheel, keeping the point under the mouse in place.
    // The pixel counts from the top-left corner of a viewport of the given size.
    void zoomAt(float wheelSteps, math::Vec2 pixel, math::Vec2 viewportSize) noexcept;

    [[nodiscard]] math::Vec3 position() const noexcept;
    [[nodiscard]] math::Quat rotation() const noexcept;
    [[nodiscard]] math::Vec3 forward() const noexcept;
    // What the view shown looks at: the pivot in 3D, the center of the view on the XY plane in 2D.
    [[nodiscard]] math::Vec3 pivot() const noexcept;
    // The pivot of the 3D view, whichever view is shown.
    [[nodiscard]] math::Vec3 pivot3D() const noexcept;
    // World to view transform.
    [[nodiscard]] math::Mat4 view() const noexcept;
    // Movement speed while flying, in meters per second.
    [[nodiscard]] float speed() const noexcept;
    // Yaw and pitch in degrees.
    [[nodiscard]] float yaw() const noexcept;
    [[nodiscard]] float pitch() const noexcept;
    [[nodiscard]] float distance() const noexcept;

    // Places the 3D view at a position, looking at a target.
    void lookAt(math::Vec3 position, math::Vec3 target) noexcept;
    // Restores a saved 3D view.
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
    bool m_twoD = false;
    math::Vec2 m_center{0.0f};
    float m_orthographicSize = 5.0f;
};

} // namespace devex::tools::detail
