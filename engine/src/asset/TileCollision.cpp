#include <devex/asset/TilesetData.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace devex::asset
{
namespace
{

constexpr double epsilon = 1.0e-8;

double turn(math::Vec2 a, math::Vec2 b, math::Vec2 c)
{
    return (static_cast<double>(b.x) - a.x) * (static_cast<double>(c.y) - a.y) -
           (static_cast<double>(b.y) - a.y) * (static_cast<double>(c.x) - a.x);
}

bool onSegment(math::Vec2 a, math::Vec2 b, math::Vec2 p)
{
    return std::abs(turn(a, b, p)) <= epsilon && p.x >= std::min(a.x, b.x) - epsilon &&
           p.x <= std::max(a.x, b.x) + epsilon && p.y >= std::min(a.y, b.y) - epsilon &&
           p.y <= std::max(a.y, b.y) + epsilon;
}

bool intersects(math::Vec2 a, math::Vec2 b, math::Vec2 c, math::Vec2 d)
{
    const double abC = turn(a, b, c), abD = turn(a, b, d);
    const double cdA = turn(c, d, a), cdB = turn(c, d, b);
    const bool proper = ((abC > epsilon && abD < -epsilon) || (abC < -epsilon && abD > epsilon)) &&
                        ((cdA > epsilon && cdB < -epsilon) || (cdA < -epsilon && cdB > epsilon));
    return proper || onSegment(a, b, c) || onSegment(a, b, d) || onSegment(c, d, a) ||
           onSegment(c, d, b);
}

double area(std::span<const math::Vec2> points)
{
    double twice = 0;
    for (std::size_t i = 0; i < points.size(); ++i)
        twice += turn({}, points[i], points[(i + 1) % points.size()]);
    return twice;
}

void removeCollinear(std::vector<math::Vec2>& points)
{
    for (std::size_t i = 0; points.size() > 3 && i < points.size();)
    {
        if (std::abs(turn(points[(i + points.size() - 1) % points.size()], points[i],
                          points[(i + 1) % points.size()])) <= epsilon)
        {
            points.erase(points.begin() + static_cast<std::ptrdiff_t>(i));
            i = 0;
        }
        else
            ++i;
    }
}

bool convex(std::span<const math::Vec2> points)
{
    for (std::size_t i = 0; i < points.size(); ++i)
        if (turn(points[i], points[(i + 1) % points.size()], points[(i + 2) % points.size()]) <=
            epsilon)
            return false;
    return true;
}

// Adjacent triangles are joined where their union stays convex, reducing internal edges and the
// number of Box2D shapes. The bound is Box2D's eight vertices per convex polygon.
void mergePieces(std::vector<std::vector<math::Vec2>>& pieces)
{
    for (std::size_t first = 0; first < pieces.size(); ++first)
        for (std::size_t second = first + 1; second < pieces.size();)
        {
            bool merged = false;
            for (std::size_t a = 0; a < pieces[first].size() && !merged; ++a)
                for (std::size_t b = 0; b < pieces[second].size() && !merged; ++b)
                {
                    const auto& p = pieces[first];
                    const auto& q = pieces[second];
                    if (p[a] != q[(b + 1) % q.size()] || p[(a + 1) % p.size()] != q[b])
                        continue;
                    std::vector<math::Vec2> joined;
                    for (std::size_t i = 0; i < p.size(); ++i)
                        joined.push_back(p[(a + 1 + i) % p.size()]);
                    for (std::size_t i = 1; i + 1 < q.size(); ++i)
                        joined.push_back(q[(b + 1 + i) % q.size()]);
                    removeCollinear(joined);
                    if (joined.size() <= 8 && convex(joined))
                    {
                        pieces[first] = std::move(joined);
                        pieces.erase(pieces.begin() + static_cast<std::ptrdiff_t>(second));
                        merged = true;
                    }
                }
            if (merged)
                second = first + 1;
            else
                ++second;
        }
}

} // namespace

core::Result<void> validateTileCollision(std::span<const math::Vec2> points)
{
    if (points.size() < 3 || points.size() > maxTileCollisionPoints)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "a collision outline needs 3 to {} points", maxTileCollisionPoints);
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const auto a = points[i], b = points[(i + 1) % points.size()];
        if (!std::isfinite(a.x) || !std::isfinite(a.y) || a.x < 0 || a.x > 1 || a.y < 0 || a.y > 1)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "collision points must be finite and inside the cell (0 to 1)");
        if (math::dot(a - b, a - b) <= epsilon * epsilon)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "neighboring collision points must be distinct");
        const auto c = points[(i + 2) % points.size()];
        if (std::abs(turn(a, b, c)) <= epsilon && math::dot(a - b, c - b) > 0)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "collision edges must not overlap");
        for (std::size_t j = i + 1; j < points.size(); ++j)
        {
            if (j == i + 1 || (i == 0 && j + 1 == points.size()))
                continue;
            if (intersects(a, b, points[j], points[(j + 1) % points.size()]))
                return core::makeError(core::ErrorCode::InvalidArgument,
                                       "collision edges must not cross or touch each other");
        }
    }
    if (std::abs(area(points)) <= epsilon)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "the collision outline must have a nonzero area");
    return {};
}

core::Result<std::vector<std::vector<math::Vec2>>>
decomposeTileCollision(std::span<const math::Vec2> outline)
{
    if (auto valid = validateTileCollision(outline); !valid)
        return std::unexpected(valid.error());
    std::vector<math::Vec2> points(outline.begin(), outline.end());
    if (area(points) < 0)
        std::ranges::reverse(points);
    removeCollinear(points);
    if (points.size() <= 8 && convex(points))
        return std::vector<std::vector<math::Vec2>>{std::move(points)};
    std::vector<std::vector<math::Vec2>> pieces;
    std::vector<std::size_t> remaining(points.size());
    std::iota(remaining.begin(), remaining.end(), 0);
    while (remaining.size() > 3)
    {
        bool found = false;
        for (std::size_t i = 0; i < remaining.size(); ++i)
        {
            const auto a = points[remaining[(i + remaining.size() - 1) % remaining.size()]];
            const auto b = points[remaining[i]], c = points[remaining[(i + 1) % remaining.size()]];
            if (turn(a, b, c) <= epsilon)
                continue;
            const bool contains = std::ranges::any_of(remaining,
                                                      [&](std::size_t index)
                                                      {
                                                          const auto p = points[index];
                                                          return p != a && p != b && p != c &&
                                                                 turn(a, b, p) >= -epsilon &&
                                                                 turn(b, c, p) >= -epsilon &&
                                                                 turn(c, a, p) >= -epsilon;
                                                      });
            if (contains)
                continue;
            pieces.push_back({a, b, c});
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
            found = true;
            break;
        }
        if (!found)
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "the collision outline cannot be decomposed");
    }
    pieces.push_back({points[remaining[0]], points[remaining[1]], points[remaining[2]]});
    mergePieces(pieces);
    return pieces;
}

} // namespace devex::asset
