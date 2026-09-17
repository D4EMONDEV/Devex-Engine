#include "ToolsState.hpp"

#include <devex/scene/Components.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <unordered_set>
#include <vector>

namespace devex::tools::detail {
namespace {

using render::OverlayVertex;

constexpr float twoPi = 2.0f * std::numbers::pi_v<float>;
constexpr float iconSizeInPixels = 11.0f;
constexpr math::Vec4 gridColor{0.25f, 0.25f, 0.25f, 0.55f};
constexpr math::Vec4 gridMajorColor{0.35f, 0.35f, 0.35f, 0.8f};
constexpr math::Vec4 xAxisColor{0.6f, 0.08f, 0.08f, 0.9f};
constexpr math::Vec4 zAxisColor{0.06f, 0.14f, 0.6f, 0.9f};
constexpr math::Vec4 lightIconColor{1.0f, 0.8f, 0.3f, 0.95f};
constexpr math::Vec4 cameraIconColor{0.85f, 0.85f, 0.85f, 0.95f};

void addLine(std::vector<OverlayVertex>& lines, math::Vec3 start, math::Vec3 end, math::Vec4 color)
{
    lines.push_back({start, color});
    lines.push_back({end, color});
}

void addCircle(std::vector<OverlayVertex>& lines, math::Vec3 center, math::Vec3 u, math::Vec3 v, float radius,
               math::Vec4 color, int segments = 32)
{
    for (int segment = 0; segment < segments; ++segment)
    {
        const float angle0 = twoPi * static_cast<float>(segment) / static_cast<float>(segments);
        const float angle1 = twoPi * static_cast<float>(segment + 1) / static_cast<float>(segments);
        addLine(lines, center + (u * std::cos(angle0) + v * std::sin(angle0)) * radius,
                center + (u * std::cos(angle1) + v * std::sin(angle1)) * radius, color);
    }
}

[[nodiscard]] math::Vec3 forwardOf(const math::Mat4& world)
{
    const math::Vec3 direction = math::Mat3(world) * math::Vec3{0.0f, 0.0f, -1.0f};
    return math::length(direction) > 0.0f ? math::normalize(direction) : math::Vec3{0.0f, 0.0f, -1.0f};
}

// Lines on the ground around the camera, fading with distance, every meter or every ten meters
// from high up.
void addGrid(const ToolsState& state, std::vector<OverlayVertex>& lines)
{
    const math::Vec3 eye = state.camera.position();
    const float height = std::abs(eye.y);
    const float spacing = height > 60.0f ? 10.0f : 1.0f;
    const float extent = std::clamp(height * 6.0f, 30.0f, 600.0f);
    const int count = static_cast<int>(extent / spacing);
    const float centerX = std::round(eye.x / spacing) * spacing;
    const float centerZ = std::round(eye.z / spacing) * spacing;
    constexpr int pieces = 8;

    const auto fade = [&](math::Vec3 point, math::Vec4 color) {
        const float distance = math::length(math::Vec3{point.x - eye.x, 0.0f, point.z - eye.z});
        color.a *= std::clamp(1.0f - distance / extent, 0.0f, 1.0f);
        return color;
    };
    const auto addFaded = [&](math::Vec3 start, math::Vec3 end, math::Vec4 color) {
        // Pieces let the fade vary along long lines.
        for (int piece = 0; piece < pieces; ++piece)
        {
            const math::Vec3 from = math::mix(start, end, static_cast<float>(piece) / pieces);
            const math::Vec3 to = math::mix(start, end, static_cast<float>(piece + 1) / pieces);
            lines.push_back({from, fade(from, color)});
            lines.push_back({to, fade(to, color)});
        }
    };

    for (int index = -count; index <= count; ++index)
    {
        const float x = centerX + static_cast<float>(index) * spacing;
        const float z = centerZ + static_cast<float>(index) * spacing;
        const bool majorX = std::fmod(std::abs(x), spacing * 10.0f) < 0.5f * spacing;
        const bool majorZ = std::fmod(std::abs(z), spacing * 10.0f) < 0.5f * spacing;
        addFaded({x, 0.0f, centerZ - extent}, {x, 0.0f, centerZ + extent},
                 std::abs(x) < 0.5f * spacing ? zAxisColor : majorX ? gridMajorColor : gridColor);
        addFaded({centerX - extent, 0.0f, z}, {centerX + extent, 0.0f, z},
                 std::abs(z) < 0.5f * spacing ? xAxisColor : majorZ ? gridMajorColor : gridColor);
    }
}

void addIcons(scene::Scene& scene, const ViewportView& view, scene::Entity selected, render::RenderWorld& world)
{
    // Billboards face the camera: its right and up axes are the rows of the view rotation.
    const math::Vec3 right{view.view[0][0], view.view[1][0], view.view[2][0]};
    const math::Vec3 up{view.view[0][1], view.view[1][1], view.view[2][1]};
    std::vector<OverlayVertex>& lines = world.overlayLines;

    for ([[maybe_unused]] auto [entity, transform, light] : scene.view<scene::WorldTransform, scene::DirectionalLight>())
    {
        const math::Vec3 position(transform.matrix[3]);
        const float size = view.worldSize(position, iconSizeInPixels);
        const math::Vec4 color = entity == selected ? math::Vec4{1.0f, 0.6f, 0.1f, 1.0f} : lightIconColor;
        addCircle(lines, position, right, up, size * 0.6f, color, 16);
        for (int ray = 0; ray < 8; ++ray)
        {
            const float angle = twoPi * static_cast<float>(ray) / 8.0f;
            const math::Vec3 direction = right * std::cos(angle) + up * std::sin(angle);
            addLine(lines, position + direction * size * 0.85f, position + direction * size * 1.2f, color);
        }
        addLine(lines, position, position + forwardOf(transform.matrix) * size * 4.0f, color);
    }

    for ([[maybe_unused]] auto [entity, transform, light] : scene.view<scene::WorldTransform, scene::PointLight>())
    {
        const math::Vec3 position(transform.matrix[3]);
        const float size = view.worldSize(position, iconSizeInPixels);
        math::Vec4 color(math::Vec3(light.color), 0.95f);
        addCircle(lines, position, right, up, size * 0.6f, color, 16);
        addLine(lines, position - right * size * 0.3f, position + right * size * 0.3f, color);
        addLine(lines, position - up * size * 0.3f, position + up * size * 0.3f, color);
        if (entity == selected)
        {
            color.a = 0.5f;
            const math::Vec3 x{1.0f, 0.0f, 0.0f};
            const math::Vec3 y{0.0f, 1.0f, 0.0f};
            const math::Vec3 z{0.0f, 0.0f, 1.0f};
            addCircle(lines, position, x, y, light.range, color, 64);
            addCircle(lines, position, y, z, light.range, color, 64);
            addCircle(lines, position, z, x, light.range, color, 64);
        }
    }

    for ([[maybe_unused]] auto [entity, transform, light] : scene.view<scene::WorldTransform, scene::SpotLight>())
    {
        const math::Vec3 position(transform.matrix[3]);
        const math::Vec3 direction = forwardOf(transform.matrix);
        const float size = view.worldSize(position, iconSizeInPixels);
        math::Vec4 color(math::Vec3(light.color), 0.95f);
        addCircle(lines, position, right, up, size * 0.6f, color, 16);
        addLine(lines, position, position + direction * size * 2.5f, color);
        if (entity == selected)
        {
            color.a = 0.6f;
            const float angle = std::clamp(light.outerAngle, 0.0f, math::radians(89.0f));
            const math::Vec3 rimCenter = position + direction * light.range * std::cos(angle);
            const float rimRadius = light.range * std::sin(angle);
            const math::Vec3 helper = std::abs(direction.y) < 0.9f ? math::Vec3{0.0f, 1.0f, 0.0f} : math::Vec3{1.0f, 0.0f, 0.0f};
            const math::Vec3 u = math::normalize(math::cross(direction, helper));
            const math::Vec3 v = math::cross(direction, u);
            addCircle(lines, rimCenter, u, v, rimRadius, color, 48);
            for (const math::Vec3 edge : {u, -u, v, -v})
            {
                addLine(lines, position, rimCenter + edge * rimRadius, color);
            }
        }
    }

    const float aspect = view.size.x / std::max(view.size.y, 1.0f);
    for ([[maybe_unused]] auto [entity, transform, camera] : scene.view<scene::WorldTransform, scene::Camera>())
    {
        const math::Vec3 position(transform.matrix[3]);
        const math::Mat3 axes(transform.matrix);
        const math::Vec3 forward = math::normalize(axes * math::Vec3{0.0f, 0.0f, -1.0f});
        const math::Vec3 cameraUp = math::normalize(axes * math::Vec3{0.0f, 1.0f, 0.0f});
        const math::Vec3 cameraRight = math::normalize(axes * math::Vec3{1.0f, 0.0f, 0.0f});
        // A small frustum, or one reaching a few meters for the selected camera.
        const float depth = entity == selected ? 3.0f : view.worldSize(position, iconSizeInPixels * 2.5f);
        const float halfHeight = depth * std::tan(camera.verticalFov * 0.5f);
        const float halfWidth = halfHeight * aspect;
        const math::Vec4 color = entity == selected ? math::Vec4{1.0f, 0.6f, 0.1f, 1.0f} : cameraIconColor;
        const math::Vec3 center = position + forward * depth;
        const std::array<math::Vec3, 4> corners{center + cameraRight * halfWidth + cameraUp * halfHeight,
                                                center - cameraRight * halfWidth + cameraUp * halfHeight,
                                                center - cameraRight * halfWidth - cameraUp * halfHeight,
                                                center + cameraRight * halfWidth - cameraUp * halfHeight};
        for (std::size_t corner = 0; corner < corners.size(); ++corner)
        {
            addLine(lines, position, corners[corner], color);
            addLine(lines, corners[corner], corners[(corner + 1) % corners.size()], color);
        }
        // A notch marks the top of the image.
        addLine(lines, center + cameraUp * halfHeight * 1.3f, center + cameraUp * halfHeight * 1.05f, color);
    }
}

// Indices of the entity and of its descendants.
void collectSubtree(const scene::Scene& scene, scene::Entity entity, std::unordered_set<std::uint32_t>& indices)
{
    indices.insert(entity.index);
    for (scene::Entity child = scene.firstChild(entity); child.isValid(); child = scene.nextSibling(child))
    {
        collectSubtree(scene, child, indices);
    }
}

} // namespace

void addEditorOverlay(ToolsState& state, scene::Scene& scene, render::RenderWorld& world)
{
    const ViewportView view{
        .view = world.camera.view,
        .verticalFov = world.camera.verticalFov,
        .size = {static_cast<float>(std::max(world.viewport.width, 1u)), static_cast<float>(std::max(world.viewport.height, 1u))},
    };
    const scene::Entity selected = scene.findEntity(state.selection);

    addGrid(state, world.sceneLines);
    addIcons(scene, view, selected, world);

    if (selected.isValid())
    {
        std::unordered_set<std::uint32_t> outlined;
        collectSubtree(scene, selected, outlined);
        for (render::MeshInstance& mesh : world.meshes)
        {
            mesh.outlined = mesh.objectId > 0 && outlined.contains(mesh.objectId - 1);
        }

        if (const scene::WorldTransform* const transform = scene.tryGet<scene::WorldTransform>(selected);
            transform != nullptr && scene.has<scene::Transform>(selected))
        {
            GizmoGeometry geometry;
            state.gizmo.draw(view, transform->matrix, state.hoveredHandle, geometry);
            world.overlayTriangles.insert(world.overlayTriangles.end(), geometry.triangles.begin(), geometry.triangles.end());
            world.overlayLines.insert(world.overlayLines.end(), geometry.lines.begin(), geometry.lines.end());
        }
    }

    if (state.pickPixel)
    {
        const math::Vec2 pixel = *std::exchange(state.pickPixel, std::nullopt);
        if (pixel.x >= 0.0f && pixel.y >= 0.0f)
        {
            const std::uint64_t id = state.nextPickId++;
            world.pick = render::PickRequest{
                .x = static_cast<std::uint32_t>(pixel.x),
                .y = static_cast<std::uint32_t>(pixel.y),
                .id = id,
            };
            state.awaitedPick = id;
        }
    }
}

} // namespace devex::tools::detail
