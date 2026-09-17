#include "Gizmo.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>

namespace devex::tools::detail {
namespace {

constexpr float twoPi = 2.0f * std::numbers::pi_v<float>;
constexpr int ringSegments = 64;
constexpr float viewRingScale = 1.15f;
constexpr float planeStart = 0.2f;
constexpr float planeEnd = 0.45f;

// Linear colors, from the sRGB colors editors traditionally give to the axes.
constexpr math::Vec4 axisColors[3]{
    {0.78f, 0.05f, 0.05f, 1.0f},
    {0.19f, 0.67f, 0.03f, 1.0f},
    {0.04f, 0.18f, 0.87f, 1.0f},
};
constexpr math::Vec4 highlightColor{1.0f, 0.69f, 0.0f, 1.0f};
constexpr math::Vec4 viewColor{0.8f, 0.8f, 0.8f, 1.0f};

[[nodiscard]] int axisIndex(GizmoHandle handle) noexcept
{
    switch (handle)
    {
    case GizmoHandle::X:
    case GizmoHandle::YZ:
        return 0;
    case GizmoHandle::Y:
    case GizmoHandle::ZX:
        return 1;
    case GizmoHandle::Z:
    case GizmoHandle::XY:
        return 2;
    default:
        return -1;
    }
}

[[nodiscard]] bool isPlane(GizmoHandle handle) noexcept
{
    return handle == GizmoHandle::XY || handle == GizmoHandle::YZ || handle == GizmoHandle::ZX;
}

// The two axes spanning a plane handle.
[[nodiscard]] std::pair<int, int> planeAxes(GizmoHandle handle) noexcept
{
    switch (handle)
    {
    case GizmoHandle::XY:
        return {0, 1};
    case GizmoHandle::YZ:
        return {1, 2};
    default:
        return {2, 0};
    }
}

constexpr std::array<GizmoHandle, 3> axisHandles{GizmoHandle::X, GizmoHandle::Y, GizmoHandle::Z};
constexpr std::array<GizmoHandle, 3> planeHandles{GizmoHandle::YZ, GizmoHandle::ZX, GizmoHandle::XY};

// Two unit vectors perpendicular to a unit axis and to each other.
[[nodiscard]] std::pair<math::Vec3, math::Vec3> perpendicularBasis(math::Vec3 axis) noexcept
{
    const math::Vec3 helper = std::abs(axis.y) < 0.9f ? math::Vec3{0.0f, 1.0f, 0.0f} : math::Vec3{1.0f, 0.0f, 0.0f};
    const math::Vec3 first = math::normalize(math::cross(axis, helper));
    return {first, math::cross(axis, first)};
}

[[nodiscard]] bool insideQuad(math::Vec2 point, const std::array<math::Vec2, 4>& corners) noexcept
{
    bool positive = false;
    bool negative = false;
    for (std::size_t index = 0; index < corners.size(); ++index)
    {
        const math::Vec2 edge = corners[(index + 1) % corners.size()] - corners[index];
        const math::Vec2 toPoint = point - corners[index];
        const float side = edge.x * toPoint.y - edge.y * toPoint.x;
        positive = positive || side > 0.0f;
        negative = negative || side < 0.0f;
    }
    return !(positive && negative);
}

[[nodiscard]] float snapTo(float value, float step) noexcept
{
    return std::round(value / step) * step;
}

void addTriangle(GizmoGeometry& geometry, math::Vec3 a, math::Vec3 b, math::Vec3 c, math::Vec4 color)
{
    geometry.triangles.push_back({a, color});
    geometry.triangles.push_back({b, color});
    geometry.triangles.push_back({c, color});
}

void addQuad(GizmoGeometry& geometry, math::Vec3 a, math::Vec3 b, math::Vec3 c, math::Vec3 d, math::Vec4 color)
{
    addTriangle(geometry, a, b, c, color);
    addTriangle(geometry, a, c, d, color);
}

// A tube along a segment, with a few sides.
void addTube(GizmoGeometry& geometry, math::Vec3 start, math::Vec3 end, float radius, math::Vec4 color)
{
    constexpr int sides = 6;
    const math::Vec3 axis = math::normalize(end - start);
    const auto [first, second] = perpendicularBasis(axis);
    for (int side = 0; side < sides; ++side)
    {
        const float angle0 = twoPi * static_cast<float>(side) / sides;
        const float angle1 = twoPi * static_cast<float>(side + 1) / sides;
        const math::Vec3 offset0 = (first * std::cos(angle0) + second * std::sin(angle0)) * radius;
        const math::Vec3 offset1 = (first * std::cos(angle1) + second * std::sin(angle1)) * radius;
        addQuad(geometry, start + offset0, start + offset1, end + offset1, end + offset0, color);
    }
}

void addCone(GizmoGeometry& geometry, math::Vec3 base, math::Vec3 tip, float radius, math::Vec4 color)
{
    constexpr int sides = 12;
    const math::Vec3 axis = math::normalize(tip - base);
    const auto [first, second] = perpendicularBasis(axis);
    for (int side = 0; side < sides; ++side)
    {
        const float angle0 = twoPi * static_cast<float>(side) / sides;
        const float angle1 = twoPi * static_cast<float>(side + 1) / sides;
        const math::Vec3 rim0 = base + (first * std::cos(angle0) + second * std::sin(angle0)) * radius;
        const math::Vec3 rim1 = base + (first * std::cos(angle1) + second * std::sin(angle1)) * radius;
        // Sides facing away are darker, which gives the cone some volume without lighting.
        const float shade = 0.75f + 0.25f * std::cos(angle0);
        addTriangle(geometry, rim0, rim1, tip, math::Vec4(math::Vec3(color) * shade, color.a));
        addTriangle(geometry, rim1, rim0, base, color);
    }
}

void addCube(GizmoGeometry& geometry, math::Vec3 center, const std::array<math::Vec3, 3>& axes, float halfSize,
             math::Vec4 color)
{
    for (int face = 0; face < 3; ++face)
    {
        const math::Vec3 normal = axes[face] * halfSize;
        const math::Vec3 u = axes[(face + 1) % 3] * halfSize;
        const math::Vec3 v = axes[(face + 2) % 3] * halfSize;
        for (const float sign : {1.0f, -1.0f})
        {
            const math::Vec3 middle = center + normal * sign;
            const math::Vec4 shaded(math::Vec3(color) * (sign > 0.0f ? 1.0f : 0.7f), color.a);
            addQuad(geometry, middle - u - v, middle + u - v, middle + u + v, middle - u + v, shaded);
        }
    }
}

// A ring around an axis, drawn as a tube. Its half facing away from the camera is faded.
void addRing(GizmoGeometry& geometry, const ViewportView& view, math::Vec3 center, math::Vec3 axis, float radius,
             float thickness, math::Vec4 color, bool fadeBack)
{
    const auto [first, second] = perpendicularBasis(axis);
    const math::Vec3 toCamera = view.cameraPosition() - center;
    for (int segment = 0; segment < ringSegments; ++segment)
    {
        const float angle0 = twoPi * static_cast<float>(segment) / ringSegments;
        const float angle1 = twoPi * static_cast<float>(segment + 1) / ringSegments;
        const math::Vec3 direction0 = first * std::cos(angle0) + second * std::sin(angle0);
        const math::Vec3 direction1 = first * std::cos(angle1) + second * std::sin(angle1);
        math::Vec4 segmentColor = color;
        if (fadeBack && math::dot(direction0 + direction1, toCamera) < 0.0f)
        {
            segmentColor.a *= 0.25f;
        }
        const math::Vec3 point0 = center + direction0 * radius;
        const math::Vec3 point1 = center + direction1 * radius;
        // One band lies in the plane of the ring, the other along its axis, so that the ring keeps
        // some thickness from any angle.
        const math::Vec3 radial0 = direction0 * thickness;
        const math::Vec3 radial1 = direction1 * thickness;
        const math::Vec3 along = axis * thickness;
        addQuad(geometry, point0 - radial0, point1 - radial1, point1 + radial1, point0 + radial0, segmentColor);
        addQuad(geometry, point0 - along, point1 - along, point1 + along, point0 + along, segmentColor);
    }
}

} // namespace

std::array<math::Vec3, 3> Gizmo::axes(const math::Mat4& world) const noexcept
{
    std::array<math::Vec3, 3> result{math::Vec3{1.0f, 0.0f, 0.0f}, math::Vec3{0.0f, 1.0f, 0.0f},
                                     math::Vec3{0.0f, 0.0f, 1.0f}};
    if (mode == GizmoMode::Scale || space == GizmoSpace::Local)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            const math::Vec3 column(world[axis]);
            if (math::length(column) > 1e-6f)
            {
                result[static_cast<std::size_t>(axis)] = math::normalize(column);
            }
        }
    }
    return result;
}

GizmoHandle Gizmo::hitTest(const ViewportView& view, const math::Mat4& world, math::Vec2 mouse) const
{
    const math::Vec3 center(world[3]);
    const std::optional<math::Vec2> centerPixel = view.project(center);
    if (!centerPixel)
    {
        return GizmoHandle::None;
    }
    const float size = view.worldSize(center, sizeInPixels);
    const std::array<math::Vec3, 3> gizmoAxes = axes(world);

    GizmoHandle best = GizmoHandle::None;
    float bestDistance = pickDistanceInPixels;
    const auto consider = [&](GizmoHandle handle, float distance) {
        if (distance < bestDistance)
        {
            best = handle;
            bestDistance = distance;
        }
    };
    const auto axisDistance = [&](int axis) {
        const std::optional<math::Vec2> end = view.project(center + gizmoAxes[static_cast<std::size_t>(axis)] * size);
        return end ? distanceToSegment(mouse, *centerPixel, *end) : pickDistanceInPixels;
    };

    switch (mode)
    {
    case GizmoMode::Translate:
    case GizmoMode::Scale: {
        // The center handle wins where it overlaps the axes.
        if (math::length(mouse - *centerPixel) < pickDistanceInPixels)
        {
            return GizmoHandle::View;
        }
        if (mode == GizmoMode::Translate)
        {
            for (const GizmoHandle handle : planeHandles)
            {
                const auto [first, second] = planeAxes(handle);
                const math::Vec3 a = gizmoAxes[static_cast<std::size_t>(first)] * size;
                const math::Vec3 b = gizmoAxes[static_cast<std::size_t>(second)] * size;
                std::array<math::Vec2, 4> corners{};
                bool visible = true;
                const std::array<math::Vec3, 4> points{center + a * planeStart + b * planeStart,
                                                       center + a * planeEnd + b * planeStart,
                                                       center + a * planeEnd + b * planeEnd,
                                                       center + a * planeStart + b * planeEnd};
                for (std::size_t corner = 0; corner < points.size(); ++corner)
                {
                    const std::optional<math::Vec2> projected = view.project(points[corner]);
                    visible = visible && projected.has_value();
                    corners[corner] = projected.value_or(math::Vec2{0.0f});
                }
                if (visible && insideQuad(mouse, corners))
                {
                    return handle;
                }
            }
        }
        for (int axis = 0; axis < 3; ++axis)
        {
            consider(axisHandles[static_cast<std::size_t>(axis)], axisDistance(axis));
        }
        break;
    }
    case GizmoMode::Rotate: {
        const math::Vec3 toCamera = view.cameraPosition() - center;
        for (int axis = 0; axis < 3; ++axis)
        {
            const auto [first, second] = perpendicularBasis(gizmoAxes[static_cast<std::size_t>(axis)]);
            std::optional<math::Vec2> previous;
            math::Vec3 previousDirection{0.0f};
            for (int segment = 0; segment <= ringSegments; ++segment)
            {
                const float angle = twoPi * static_cast<float>(segment) / ringSegments;
                const math::Vec3 direction = first * std::cos(angle) + second * std::sin(angle);
                const std::optional<math::Vec2> point = view.project(center + direction * size);
                // The back half of a ring is hidden behind the front half.
                if (previous && point && math::dot(direction + previousDirection, toCamera) >= -1e-3f * size)
                {
                    consider(axisHandles[static_cast<std::size_t>(axis)], distanceToSegment(mouse, *previous, *point));
                }
                previous = point;
                previousDirection = direction;
            }
        }
        consider(GizmoHandle::View,
                 std::abs(math::length(mouse - *centerPixel) - sizeInPixels * viewRingScale));
        break;
    }
    }
    return best;
}

void Gizmo::begin(GizmoHandle handle, const ViewportView& view, const math::Mat4& world, const math::Mat4& parentWorld,
                  const scene::Transform& local, math::Vec2 mouse)
{
    if (handle == GizmoHandle::None)
    {
        return;
    }
    m_handle = handle;
    m_startView = view;
    m_startWorld = world;
    m_parentWorld = parentWorld;
    m_startLocal = local;
    m_startMouse = mouse;
    m_startAxes = axes(world);
    m_startSize = view.worldSize(math::Vec3(world[3]), sizeInPixels);
}

scene::Transform Gizmo::drag(const ViewportView& view, math::Vec2 mouse, bool snap) const
{
    scene::Transform result = m_startLocal;
    if (m_handle == GizmoHandle::None)
    {
        return result;
    }

    const math::Vec3 center(m_startWorld[3]);
    const Ray startRay = m_startView.ray(m_startMouse);
    const Ray ray = view.ray(mouse);
    const math::Mat4 parentInverse = math::inverse(m_parentWorld);
    const int axis = axisIndex(m_handle);

    switch (mode)
    {
    case GizmoMode::Translate: {
        math::Vec3 delta{0.0f};
        if (isPlane(m_handle) || m_handle == GizmoHandle::View)
        {
            const math::Vec3 normal = m_handle == GizmoHandle::View ? m_startView.cameraForward()
                                                                    : m_startAxes[static_cast<std::size_t>(axis)];
            const std::optional<math::Vec3> startHit = intersectPlane(startRay, center, normal);
            const std::optional<math::Vec3> hit = intersectPlane(ray, center, normal);
            if (startHit && hit)
            {
                delta = *hit - *startHit;
                if (snap && isPlane(m_handle))
                {
                    const auto [first, second] = planeAxes(m_handle);
                    const math::Vec3 u = m_startAxes[static_cast<std::size_t>(first)];
                    const math::Vec3 v = m_startAxes[static_cast<std::size_t>(second)];
                    delta = u * snapTo(math::dot(delta, u), translationSnap) + v * snapTo(math::dot(delta, v), translationSnap);
                }
                else if (snap)
                {
                    delta = {snapTo(delta.x, translationSnap), snapTo(delta.y, translationSnap),
                             snapTo(delta.z, translationSnap)};
                }
            }
        }
        else
        {
            const math::Vec3 direction = m_startAxes[static_cast<std::size_t>(axis)];
            const std::optional<float> start = closestParameterOnLine(center, direction, startRay);
            const std::optional<float> current = closestParameterOnLine(center, direction, ray);
            if (start && current)
            {
                const float distance = *current - *start;
                delta = direction * (snap ? snapTo(distance, translationSnap) : distance);
            }
        }
        result.position = m_startLocal.position + math::Vec3(parentInverse * math::Vec4(delta, 0.0f));
        break;
    }
    case GizmoMode::Rotate: {
        const math::Vec3 rotationAxis = m_handle == GizmoHandle::View ? -m_startView.cameraForward()
                                                                      : m_startAxes[static_cast<std::size_t>(axis)];
        float angle = 0.0f;
        const std::optional<math::Vec3> startHit = intersectPlane(startRay, center, rotationAxis);
        const std::optional<math::Vec3> hit = intersectPlane(ray, center, rotationAxis);
        if (std::abs(math::dot(startRay.direction, rotationAxis)) > 0.1f && startHit && hit &&
            math::length(*startHit - center) > 1e-5f && math::length(*hit - center) > 1e-5f)
        {
            const math::Vec3 from = *startHit - center;
            const math::Vec3 to = *hit - center;
            angle = std::atan2(math::dot(rotationAxis, math::cross(from, to)), math::dot(from, to));
        }
        else
        {
            // Seen edge-on, the ring turns with horizontal mouse movement.
            angle = (mouse.x - m_startMouse.x) * 0.01f;
        }
        if (snap)
        {
            angle = snapTo(angle, rotationSnap);
        }
        const math::Quat worldDelta = math::angleAxis(angle, rotationAxis);
        const math::Quat startRotation = math::decomposeTrs(m_startWorld).rotation;
        const math::Quat parentRotation = math::decomposeTrs(m_parentWorld).rotation;
        result.rotation = math::normalize(math::inverse(parentRotation) * worldDelta * startRotation);
        break;
    }
    case GizmoMode::Scale: {
        if (m_handle == GizmoHandle::View)
        {
            const float factor = std::max(1.0f + ((mouse.x - m_startMouse.x) - (mouse.y - m_startMouse.y)) / 100.0f, 0.01f);
            result.scale = m_startLocal.scale * factor;
            if (snap)
            {
                result.scale = {snapTo(result.scale.x, scaleSnap), snapTo(result.scale.y, scaleSnap),
                                snapTo(result.scale.z, scaleSnap)};
            }
        }
        else if (axis >= 0)
        {
            const math::Vec3 direction = m_startAxes[static_cast<std::size_t>(axis)];
            const std::optional<float> start = closestParameterOnLine(center, direction, startRay);
            const std::optional<float> current = closestParameterOnLine(center, direction, ray);
            if (start && current)
            {
                const float factor = std::max(1.0f + (*current - *start) / m_startSize, 0.01f);
                float scaled = m_startLocal.scale[axis] * factor;
                if (snap)
                {
                    scaled = snapTo(scaled, scaleSnap);
                }
                result.scale[axis] = scaled;
            }
        }
        break;
    }
    }
    return result;
}

void Gizmo::end() noexcept
{
    m_handle = GizmoHandle::None;
}

bool Gizmo::isDragging() const noexcept
{
    return m_handle != GizmoHandle::None;
}

GizmoHandle Gizmo::activeHandle() const noexcept
{
    return m_handle;
}

const scene::Transform& Gizmo::startTransform() const noexcept
{
    return m_startLocal;
}

void Gizmo::draw(const ViewportView& view, const math::Mat4& world, GizmoHandle hovered, GizmoGeometry& geometry) const
{
    const math::Vec3 center(world[3]);
    if (!view.project(center))
    {
        return;
    }
    const float size = view.worldSize(center, sizeInPixels);
    const float thickness = view.worldSize(center, 1.5f);
    const std::array<math::Vec3, 3> gizmoAxes = axes(world);
    const GizmoHandle highlighted = isDragging() ? m_handle : hovered;
    const auto colorOf = [&](GizmoHandle handle, math::Vec4 color) {
        return handle == highlighted ? highlightColor : color;
    };

    // Farther axes first, since handles are drawn over each other without depth.
    std::array<int, 3> order{0, 1, 2};
    const math::Vec3 cameraPosition = view.cameraPosition();
    std::ranges::sort(order, [&](int left, int right) {
        return math::length(center + gizmoAxes[static_cast<std::size_t>(left)] * size - cameraPosition) >
               math::length(center + gizmoAxes[static_cast<std::size_t>(right)] * size - cameraPosition);
    });

    switch (mode)
    {
    case GizmoMode::Translate: {
        for (const GizmoHandle handle : planeHandles)
        {
            const auto [first, second] = planeAxes(handle);
            const math::Vec3 a = gizmoAxes[static_cast<std::size_t>(first)] * size;
            const math::Vec3 b = gizmoAxes[static_cast<std::size_t>(second)] * size;
            math::Vec4 color = colorOf(handle, axisColors[axisIndex(handle)]);
            color.a = handle == highlighted ? 0.8f : 0.45f;
            addQuad(geometry, center + a * planeStart + b * planeStart, center + a * planeEnd + b * planeStart,
                    center + a * planeEnd + b * planeEnd, center + a * planeStart + b * planeEnd, color);
        }
        for (const int axis : order)
        {
            const GizmoHandle handle = axisHandles[static_cast<std::size_t>(axis)];
            const math::Vec3 direction = gizmoAxes[static_cast<std::size_t>(axis)];
            const math::Vec4 color = colorOf(handle, axisColors[axis]);
            addTube(geometry, center, center + direction * size * 0.78f, thickness, color);
            addCone(geometry, center + direction * size * 0.78f, center + direction * size, size * 0.065f, color);
        }
        addCube(geometry, center, {view.cameraForward(), math::Vec3{0.0f, 1.0f, 0.0f}, math::Vec3{1.0f, 0.0f, 0.0f}},
                size * 0.04f, colorOf(GizmoHandle::View, viewColor));
        break;
    }
    case GizmoMode::Rotate: {
        for (const int axis : order)
        {
            const GizmoHandle handle = axisHandles[static_cast<std::size_t>(axis)];
            addRing(geometry, view, center, gizmoAxes[static_cast<std::size_t>(axis)], size, thickness,
                    colorOf(handle, axisColors[axis]), true);
        }
        addRing(geometry, view, center, -view.cameraForward(), size * viewRingScale, thickness,
                colorOf(GizmoHandle::View, viewColor), false);
        break;
    }
    case GizmoMode::Scale: {
        for (const int axis : order)
        {
            const GizmoHandle handle = axisHandles[static_cast<std::size_t>(axis)];
            const math::Vec3 direction = gizmoAxes[static_cast<std::size_t>(axis)];
            const math::Vec4 color = colorOf(handle, axisColors[axis]);
            addTube(geometry, center, center + direction * size * 0.92f, thickness, color);
            addCube(geometry, center + direction * size * 0.92f, gizmoAxes, size * 0.06f, color);
        }
        addCube(geometry, center, gizmoAxes, size * 0.08f, colorOf(GizmoHandle::View, viewColor));
        break;
    }
    }
}

} // namespace devex::tools::detail
