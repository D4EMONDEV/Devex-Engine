#pragma once

#include <devex/core/Export.hpp>
#include <devex/math/Math.hpp>
#include <devex/reflection/Reflection.hpp>
#include <devex/scene/EntityRef.hpp>

#include <array>
#include <span>
#include <string_view>
#include <vector>

// Curves drawn in a scene, for rails, patrols and the moves of a camera, and what follows them.
namespace devex::scene {

// A Bézier curve through points, in the space of the entity, as Godot's Path3D: each point has two
// handles, relative to it, that say how the curve arrives and how it leaves.
struct DEVEX_API Path3D
{
    std::vector<math::Vec3> points;
    std::vector<math::Vec3> handlesIn;
    std::vector<math::Vec3> handlesOut;
    // The last point joins the first.
    bool closed = false;
};

enum class PathEnd : std::uint8_t
{
    // Stays at the end.
    Stop,
    // Starts over from the beginning; on a closed path, goes on around.
    Loop,
    // Goes back the other way.
    PingPong,
};

// Moves its entity along a Path3D at a speed, once per frame while the game plays.
struct DEVEX_API PathFollower
{
    // The entity of the path; none means the parent of this entity.
    EntityRef path;
    // In meters per second, along the curve.
    float speed = 2.0f;
    // How far along the curve it is, in meters: set it to put the entity somewhere.
    float distance = 0.0f;
    PathEnd end = PathEnd::Loop;
    // Looks where it goes, its -Z along the curve, rather than keeping its rotation.
    bool orient = true;
    bool playing = true;
    // Going back, on the way back of a PingPong.
    bool reversed = false;
};

DEVEX_DECLARE_ENGINE_REFLECTION(Path3D);
DEVEX_DECLARE_ENGINE_REFLECTION(PathFollower);

// A Path3D as a line of many short segments, which tell where a distance along it is. Building one walks the
// whole curve: what asks every frame keeps it while the path does not change.
class DEVEX_API PathCurve
{
public:
    explicit PathCurve(const Path3D& path);

    [[nodiscard]] bool empty() const noexcept
    {
        return m_points.empty();
    }
    [[nodiscard]] bool closed() const noexcept
    {
        return m_closed;
    }
    // In meters of the space of the path.
    [[nodiscard]] float length() const noexcept;
    // Past its ends, a distance stays at them, and goes around a closed curve.
    [[nodiscard]] math::Vec3 positionAt(float distance) const noexcept;
    // Where it goes there, normalized.
    [[nodiscard]] math::Vec3 directionAt(float distance) const noexcept;
    // How far along it the point of the curve closest to a position is.
    [[nodiscard]] float closestDistance(math::Vec3 position) const noexcept;
    // The line itself, to draw it.
    [[nodiscard]] std::span<const math::Vec3> points() const noexcept
    {
        return m_points;
    }

private:
    [[nodiscard]] float folded(float distance) const noexcept;
    [[nodiscard]] std::size_t segmentAt(float distance) const noexcept;

    std::vector<math::Vec3> m_points;
    // How far along it each point is.
    std::vector<float> m_distances;
    bool m_closed = false;
};

} // namespace devex::scene

template <>
struct devex::reflection::EnumNames<devex::scene::PathEnd>
{
    static constexpr std::array<std::string_view, 3> names{"stop", "loop", "ping_pong"};
};
