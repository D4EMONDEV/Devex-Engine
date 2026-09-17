#pragma once

#include "EditorView.hpp"

#include <devex/math/Math.hpp>
#include <devex/render/RenderWorld.hpp>
#include <devex/scene/Components.hpp>

#include <cstdint>
#include <vector>

namespace devex::tools::detail {

enum class GizmoMode : std::uint8_t
{
    Translate,
    Rotate,
    Scale,
};

enum class GizmoSpace : std::uint8_t
{
    World,
    // Along the axes of the entity. Scaling always is.
    Local,
};

enum class GizmoHandle : std::uint8_t
{
    None,
    X,
    Y,
    Z,
    // The planes between two axes, for translation.
    XY,
    YZ,
    ZX,
    // Translation in the view plane, rotation around the view direction, or uniform scaling.
    View,
};

struct GizmoGeometry
{
    std::vector<render::OverlayVertex> lines;
    std::vector<render::OverlayVertex> triangles;
};

// Handles drawn at an entity to move, rotate and scale it with the mouse, keeping a constant size on
// screen. Positions are viewport pixels; the entity is described by its world transform, the world
// transform of its parent and its local Transform, which dragging changes.
class Gizmo
{
public:
    static constexpr float sizeInPixels = 90.0f;
    static constexpr float pickDistanceInPixels = 8.0f;
    static constexpr float translationSnap = 0.5f;
    static constexpr float rotationSnap = math::radians(15.0f);
    static constexpr float scaleSnap = 0.1f;

    GizmoMode mode = GizmoMode::Translate;
    GizmoSpace space = GizmoSpace::World;

    // The handle under the mouse, closest first.
    [[nodiscard]] GizmoHandle hitTest(const ViewportView& view, const math::Mat4& world, math::Vec2 mouse) const;

    // Starts dragging a handle; nothing happens for GizmoHandle::None.
    void begin(GizmoHandle handle, const ViewportView& view, const math::Mat4& world, const math::Mat4& parentWorld,
               const scene::Transform& local, math::Vec2 mouse);
    // The local transform for the mouse at its current position. Snapping rounds the change to
    // steps of translationSnap meters, rotationSnap radians or scaleSnap.
    [[nodiscard]] scene::Transform drag(const ViewportView& view, math::Vec2 mouse, bool snap) const;
    void end() noexcept;

    [[nodiscard]] bool isDragging() const noexcept;
    [[nodiscard]] GizmoHandle activeHandle() const noexcept;
    // The transform the entity had when dragging started.
    [[nodiscard]] const scene::Transform& startTransform() const noexcept;

    // Adds the handles, highlighting the hovered or dragged one.
    void draw(const ViewportView& view, const math::Mat4& world, GizmoHandle hovered, GizmoGeometry& geometry) const;

private:
    // Unit axes of the gizmo in world space.
    [[nodiscard]] std::array<math::Vec3, 3> axes(const math::Mat4& world) const noexcept;

    GizmoHandle m_handle = GizmoHandle::None;
    ViewportView m_startView;
    math::Mat4 m_startWorld{1.0f};
    math::Mat4 m_parentWorld{1.0f};
    scene::Transform m_startLocal;
    math::Vec2 m_startMouse{0.0f};
    std::array<math::Vec3, 3> m_startAxes{};
    float m_startSize = 1.0f;
};

} // namespace devex::tools::detail
