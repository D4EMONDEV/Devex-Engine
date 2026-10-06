#include <devex/render/Light2D.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace devex::render {
namespace {

constexpr float twoPi = 2.0f * std::numbers::pi_v<float>;

[[nodiscard]] float cross(math::Vec2 a, math::Vec2 b) noexcept
{
    return a.x * b.y - a.y * b.x;
}

// Where a ray from `start` along `way` meets the edge, in lengths of `way`; nothing when it misses.
[[nodiscard]] std::optional<float> hit(math::Vec2 start, math::Vec2 way, math::Vec2 from, math::Vec2 to) noexcept
{
    const math::Vec2 edge = to - from;
    const float denominator = cross(way, edge);
    if (std::abs(denominator) < 1e-9f)
    {
        return std::nullopt;
    }
    const math::Vec2 offset = from - start;
    const float along = cross(offset, edge) / denominator;
    const float within = cross(offset, way) / denominator;
    if (along < 0.0f || within < -1e-5f || within > 1.0f + 1e-5f)
    {
        return std::nullopt;
    }
    return along;
}

[[nodiscard]] float distanceToEdge(math::Vec2 point, math::Vec2 from, math::Vec2 to) noexcept
{
    const math::Vec2 edge = to - from;
    const float lengthSquared = math::dot(edge, edge);
    const float along = lengthSquared > 0.0f ? std::clamp(math::dot(point - from, edge) / lengthSquared, 0.0f, 1.0f) : 0.0f;
    return math::length(point - (from + edge * along));
}

} // namespace

void pointShadow2D(math::Vec2 center, float radius, std::span<const RenderOccluder2D> edges, std::uint32_t mask,
                   std::span<float> distances)
{
    std::ranges::fill(distances, noOccluder2D);
    const auto count = static_cast<std::int64_t>(distances.size());
    if (count == 0)
    {
        return;
    }
    for (const RenderOccluder2D& edge : edges)
    {
        if ((edge.mask & mask) == 0)
        {
            continue;
        }
        const float distance = distanceToEdge(center, edge.from, edge.to);
        if (distance > radius || distance < 1e-5f)
        {
            continue;
        }
        // The directions the edge covers, the short way round from one end to the other.
        const math::Vec2 a = edge.from - center;
        const math::Vec2 b = edge.to - center;
        float start = std::atan2(a.y, a.x);
        float sweep = std::atan2(cross(a, b), math::dot(a, b));
        if (sweep < 0.0f)
        {
            start += sweep;
            sweep = -sweep;
        }
        const float scale = static_cast<float>(count) / twoPi;
        const auto first = static_cast<std::int64_t>(std::floor(start * scale - 0.5f));
        const auto last = static_cast<std::int64_t>(std::ceil((start + sweep) * scale - 0.5f));
        for (std::int64_t bin = first; bin <= last && bin - first < count; ++bin)
        {
            const float angle = (static_cast<float>(bin) + 0.5f) / scale;
            const std::optional<float> along = hit(center, math::Vec2{std::cos(angle), std::sin(angle)}, edge.from, edge.to);
            if (!along || *along > radius)
            {
                continue;
            }
            float& kept = distances[static_cast<std::size_t>(((bin % count) + count) % count)];
            kept = std::min(kept, *along);
        }
    }
}

DirectionalShadowFrame2D directionalShadowFrame(math::Vec2 direction, math::Vec2 low, math::Vec2 high, float reach) noexcept
{
    DirectionalShadowFrame2D frame;
    const float length = math::length(direction);
    frame.direction = length > 0.0f ? direction / length : math::Vec2{0.0f, -1.0f};
    frame.across = math::Vec2{-frame.direction.y, frame.direction.x};
    float acrossLow = std::numeric_limits<float>::max();
    float acrossHigh = std::numeric_limits<float>::lowest();
    float alongLow = std::numeric_limits<float>::max();
    float alongHigh = std::numeric_limits<float>::lowest();
    for (const math::Vec2 corner : {low, math::Vec2{high.x, low.y}, high, math::Vec2{low.x, high.y}})
    {
        acrossLow = std::min(acrossLow, math::dot(corner, frame.across));
        acrossHigh = std::max(acrossHigh, math::dot(corner, frame.across));
        alongLow = std::min(alongLow, math::dot(corner, frame.direction));
        alongHigh = std::max(alongHigh, math::dot(corner, frame.direction));
    }
    reach = std::max(reach, 0.0f);
    frame.origin = frame.across * ((acrossLow + acrossHigh) * 0.5f) + frame.direction * (alongLow - reach);
    frame.width = std::max(acrossHigh - acrossLow, 1e-3f);
    frame.length = alongHigh - alongLow + reach;
    return frame;
}

void directionalShadow2D(const DirectionalShadowFrame2D& frame, std::span<const RenderOccluder2D> edges, std::uint32_t mask,
                         std::span<float> distances)
{
    std::ranges::fill(distances, noOccluder2D);
    const auto count = static_cast<std::int64_t>(distances.size());
    if (count == 0)
    {
        return;
    }
    const float scale = static_cast<float>(count) / frame.width;
    for (const RenderOccluder2D& edge : edges)
    {
        if ((edge.mask & mask) == 0)
        {
            continue;
        }
        const float a = math::dot(edge.from - frame.origin, frame.across) + frame.width * 0.5f;
        const float b = math::dot(edge.to - frame.origin, frame.across) + frame.width * 0.5f;
        const auto first = std::max<std::int64_t>(static_cast<std::int64_t>(std::floor(std::min(a, b) * scale - 0.5f)), 0);
        const auto last = std::min<std::int64_t>(static_cast<std::int64_t>(std::ceil(std::max(a, b) * scale - 0.5f)), count - 1);
        for (std::int64_t strip = first; strip <= last; ++strip)
        {
            const float offset = (static_cast<float>(strip) + 0.5f) / scale - frame.width * 0.5f;
            const std::optional<float> along = hit(frame.origin + frame.across * offset, frame.direction, edge.from, edge.to);
            if (!along || *along > frame.length)
            {
                continue;
            }
            float& kept = distances[static_cast<std::size_t>(strip)];
            kept = std::min(kept, *along);
        }
    }
}

std::optional<std::pair<math::Vec2, math::Vec2>> visiblePlane2D(const math::Mat4& viewProjection) noexcept
{
    const math::Mat4 inverse = math::inverse(viewProjection);
    const auto unproject = [&](float x, float y, float depth) {
        const math::Vec4 point = inverse * math::Vec4(x, y, depth, 1.0f);
        return math::Vec3(point) / point.w;
    };
    math::Vec2 low{std::numeric_limits<float>::max()};
    math::Vec2 high{std::numeric_limits<float>::lowest()};
    for (const float x : {-1.0f, 1.0f})
    {
        for (const float y : {-1.0f, 1.0f})
        {
            // Two depths make the ray of the corner, whichever way depth runs.
            const math::Vec3 a = unproject(x, y, 0.25f);
            const math::Vec3 b = unproject(x, y, 0.75f);
            const float rise = b.z - a.z;
            if (std::abs(rise) < 1e-6f || !std::isfinite(rise))
            {
                return std::nullopt;
            }
            const math::Vec3 onPlane = a + (b - a) * (-a.z / rise);
            low = math::min(low, math::Vec2(onPlane));
            high = math::max(high, math::Vec2(onPlane));
        }
    }
    if (!std::isfinite(low.x) || !std::isfinite(low.y) || !std::isfinite(high.x) || !std::isfinite(high.y))
    {
        return std::nullopt;
    }
    return std::pair{low, high};
}

} // namespace devex::render
