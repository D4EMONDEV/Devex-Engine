#include <devex/scene/PathComponents.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace devex::scene {

namespace {

// How many short segments stand for each Bézier segment, enough for a smooth line and even speed.
constexpr std::size_t stepsPerSegment = 24;

[[nodiscard]] math::Vec3 handleOf(const std::vector<math::Vec3>& handles, std::size_t index) noexcept
{
    return index < handles.size() ? handles[index] : math::Vec3{0.0f};
}

[[nodiscard]] math::Vec3 bezier(math::Vec3 from, math::Vec3 leaving, math::Vec3 arriving, math::Vec3 to, float t) noexcept
{
    const float rest = 1.0f - t;
    return rest * rest * rest * from + 3.0f * rest * rest * t * leaving + 3.0f * rest * t * t * arriving + t * t * t * to;
}

} // namespace

DEVEX_REFLECT(Path3D)
{
    // Drawn in the view rather than typed: lists of three numbers are too wide for the inspector.
    type.field("points", &Path3D::points, {.hidden = true})
        .field("handles_in", &Path3D::handlesIn, {.hidden = true})
        .field("handles_out", &Path3D::handlesOut, {.hidden = true})
        .field("closed", &Path3D::closed, {.group = "Curve"});
}

DEVEX_REFLECT(PathFollower)
{
    type.field("path", &PathFollower::path, {.group = "Follow"})
        .field("speed", &PathFollower::speed)
        .field("distance", &PathFollower::distance)
        .field("end", &PathFollower::end)
        .field("orient", &PathFollower::orient)
        .field("playing", &PathFollower::playing)
        .field("reversed", &PathFollower::reversed, {.runtime = true});
}

PathCurve::PathCurve(const Path3D& path)
    : m_closed(path.closed && path.points.size() > 1)
{
    if (path.points.empty())
    {
        return;
    }
    const std::size_t count = path.points.size();
    const std::size_t segments = m_closed ? count : count - 1;
    m_points.reserve(segments * stepsPerSegment + 1);
    m_distances.reserve(segments * stepsPerSegment + 1);
    m_points.push_back(path.points.front());
    m_distances.push_back(0.0f);
    for (std::size_t segment = 0; segment < segments; ++segment)
    {
        const math::Vec3 from = path.points[segment];
        const math::Vec3 to = path.points[(segment + 1) % count];
        const math::Vec3 leaving = from + handleOf(path.handlesOut, segment);
        const math::Vec3 arriving = to + handleOf(path.handlesIn, (segment + 1) % count);
        for (std::size_t step = 1; step <= stepsPerSegment; ++step)
        {
            const float t = static_cast<float>(step) / static_cast<float>(stepsPerSegment);
            const math::Vec3 point = bezier(from, leaving, arriving, to, t);
            m_distances.push_back(m_distances.back() + math::length(point - m_points.back()));
            m_points.push_back(point);
        }
    }
}

float PathCurve::length() const noexcept
{
    return m_distances.empty() ? 0.0f : m_distances.back();
}

float PathCurve::folded(float distance) const noexcept
{
    const float total = length();
    if (total <= 0.0f)
    {
        return 0.0f;
    }
    if (!m_closed)
    {
        return std::clamp(distance, 0.0f, total);
    }
    const float wrapped = std::fmod(distance, total);
    return wrapped < 0.0f ? wrapped + total : wrapped;
}

std::size_t PathCurve::segmentAt(float distance) const noexcept
{
    const auto next = std::ranges::upper_bound(m_distances, distance);
    const auto index = static_cast<std::size_t>(std::distance(m_distances.begin(), next));
    return std::clamp<std::size_t>(index, 1, m_distances.size() - 1) - 1;
}

math::Vec3 PathCurve::positionAt(float distance) const noexcept
{
    if (m_points.size() < 2)
    {
        return m_points.empty() ? math::Vec3{0.0f} : m_points.front();
    }
    const float along = folded(distance);
    const std::size_t segment = segmentAt(along);
    const float span = m_distances[segment + 1] - m_distances[segment];
    const float amount = span > 0.0f ? (along - m_distances[segment]) / span : 0.0f;
    return math::mix(m_points[segment], m_points[segment + 1], amount);
}

math::Vec3 PathCurve::directionAt(float distance) const noexcept
{
    constexpr math::Vec3 forward{0.0f, 0.0f, -1.0f};
    if (m_points.size() < 2)
    {
        return forward;
    }
    // The first segment that moves: a point without handles can give several of no length.
    for (std::size_t segment = segmentAt(folded(distance)); segment + 1 < m_points.size(); ++segment)
    {
        const math::Vec3 step = m_points[segment + 1] - m_points[segment];
        if (const float size = math::length(step); size > 1.0e-6f)
        {
            return step / size;
        }
    }
    return forward;
}

float PathCurve::closestDistance(math::Vec3 position) const noexcept
{
    float closest = 0.0f;
    float best = std::numeric_limits<float>::max();
    for (std::size_t segment = 0; segment + 1 < m_points.size(); ++segment)
    {
        const math::Vec3 step = m_points[segment + 1] - m_points[segment];
        const float size = math::dot(step, step);
        const float t = size > 0.0f ? std::clamp(math::dot(position - m_points[segment], step) / size, 0.0f, 1.0f) : 0.0f;
        const math::Vec3 point = m_points[segment] + step * t;
        const float away = math::dot(position - point, position - point);
        if (away < best)
        {
            best = away;
            closest = m_distances[segment] + (m_distances[segment + 1] - m_distances[segment]) * t;
        }
    }
    return closest;
}

} // namespace devex::scene
