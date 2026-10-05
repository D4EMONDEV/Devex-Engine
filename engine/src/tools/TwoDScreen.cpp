#include "TwoDScreen.hpp"

#include "ToolsState.hpp"

#include <devex/scene/Components.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/tools/SceneCommands.hpp>
#include <devex/ui/Layout.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace devex::tools::detail {
namespace {

// The handles around the selected element, and the body that moves it whole.
enum class Handle : std::uint8_t
{
    None,
    Body,
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

constexpr float handleRadius = 5.0f;

// Where a point of a canvas lands on the screen, in points of the window.
struct CanvasView
{
    math::Vec2 origin{0.0f, 0.0f};
    float zoom = 1.0f;

    [[nodiscard]] math::Vec2 toScreen(math::Vec2 point) const noexcept
    {
        return math::Vec2(origin.x + point.x * zoom, origin.y + point.y * zoom);
    }

    [[nodiscard]] math::Vec2 toCanvas(math::Vec2 point) const noexcept
    {
        return math::Vec2{(point.x - origin.x) / zoom, (point.y - origin.y) / zoom};
    }
};

using LaidOutCanvas = std::pair<scene::Entity, ui::LayoutResult>;

// The canvases the game draws, laid out as the 2D screen draws them, the lowest sort order first.
void layoutCanvases(const scene::Scene& scene, math::Vec2 size, std::vector<LaidOutCanvas>& canvases)
{
    canvases.clear();
    for (auto [entity, canvas] : scene.view<scene::Canvas>())
    {
        if (!canvas.visible)
        {
            continue;
        }
        ui::LayoutResult layout;
        ui::layoutCanvas(scene, entity, size, layout);
        canvases.emplace_back(entity, std::move(layout));
    }
    std::ranges::stable_sort(canvases, [&scene](const auto& first, const auto& second) {
        return scene.get<scene::Canvas>(first.first).sortOrder < scene.get<scene::Canvas>(second.first).sortOrder;
    });
}

// How the units of a canvas land on the viewport: into the frame of the game, in points.
[[nodiscard]] CanvasView viewOf(const ToolsState& state, const InterfaceFrame& frame, const ui::LayoutResult& layout)
{
    return CanvasView{
        .origin = math::Vec2(state.viewportOrigin.x + frame.offset.x / state.pixelsPerPoint,
                         state.viewportOrigin.y + frame.offset.y / state.pixelsPerPoint),
        .zoom = frame.scale * layout.scale / state.pixelsPerPoint,
    };
}

// Whether the element draws something: a click on the screen takes images and texts, and leaves
// the containers that only place them to the scene tree, so that a panel stretched over the whole
// screen keeps the world under it within reach.
[[nodiscard]] bool isDrawn(const scene::Scene& scene, scene::Entity entity) noexcept
{
    if (scene.has<scene::UiImage>(entity))
    {
        return true;
    }
    const scene::UiText* const text = scene.tryGet<scene::UiText>(entity);
    return text != nullptr && !text->text.empty();
}

// Whether the element would stop the pointer while the game runs, which is what a click on the
// screen chooses first: a button rather than the label written on it.
[[nodiscard]] bool answersPointer(const scene::Scene& scene, scene::Entity entity) noexcept
{
    if (scene.tryGet<scene::UiButton>(entity) != nullptr)
    {
        return true;
    }
    if (const scene::UiImage* const image = scene.tryGet<scene::UiImage>(entity))
    {
        return image->raycastTarget;
    }
    if (const scene::UiText* const text = scene.tryGet<scene::UiText>(entity))
    {
        return text->raycastTarget;
    }
    return false;
}

// What a click at that point selects: the element the player would touch, and one step deeper
// every time the same spot is clicked again, so that the label inside a button stays reachable.
[[nodiscard]] core::Uuid pickAt(const ToolsState& state, const scene::Scene& scene, const InterfaceFrame& frame,
                                std::span<const LaidOutCanvas> canvases, math::Vec2 mouse, core::Uuid selected)
{
    // The elements under the point, the one drawn last first.
    std::vector<scene::Entity> hits;
    for (auto canvas = canvases.rbegin(); canvas != canvases.rend(); ++canvas)
    {
        const math::Vec2 point = viewOf(state, frame, canvas->second).toCanvas(mouse);
        for (auto rect = canvas->second.rects.rbegin(); rect != canvas->second.rects.rend(); ++rect)
        {
            if (rect->visible && rect->opacity > 0.0f && isDrawn(scene, rect->entity) && contains(*rect, point))
            {
                hits.push_back(rect->entity);
            }
        }
    }
    if (hits.empty())
    {
        return core::Uuid{};
    }

    std::size_t chosen = 0;
    for (std::size_t index = 0; index < hits.size(); ++index)
    {
        if (answersPointer(scene, hits[index]))
        {
            chosen = index;
            break;
        }
    }
    if (scene.uuid(hits[chosen]) == selected)
    {
        // The same element again: down to the one drawn over it, and back to the first at the end.
        chosen = chosen > 0 ? chosen - 1 : hits.size() - 1;
    }
    return scene.uuid(hits[chosen]);
}

// The eight handles of a rectangle, and the middle of each side.
[[nodiscard]] std::array<std::pair<Handle, math::Vec2>, 8> handlesOf(const CanvasView& view, const ui::LaidOutRect& rect)
{
    const math::Vec2 min = view.toScreen(rect.min);
    const math::Vec2 max = view.toScreen(rect.max);
    const math::Vec2 middle((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    return {{
        {Handle::TopLeft, min},
        {Handle::Top, math::Vec2(middle.x, min.y)},
        {Handle::TopRight, math::Vec2(max.x, min.y)},
        {Handle::Right, math::Vec2(max.x, middle.y)},
        {Handle::BottomRight, max},
        {Handle::Bottom, math::Vec2(middle.x, max.y)},
        {Handle::BottomLeft, math::Vec2(min.x, max.y)},
        {Handle::Left, math::Vec2(min.x, middle.y)},
    }};
}

[[nodiscard]] Handle handleUnder(const CanvasView& view, const ui::LaidOutRect& rect, math::Vec2 mouse)
{
    for (const auto& [handle, point] : handlesOf(view, rect))
    {
        if (std::abs(mouse.x - point.x) <= handleRadius + 2.0f && std::abs(mouse.y - point.y) <= handleRadius + 2.0f)
        {
            return handle;
        }
    }
    const math::Vec2 min = view.toScreen(rect.min);
    const math::Vec2 max = view.toScreen(rect.max);
    const bool inside = mouse.x >= min.x && mouse.x <= max.x && mouse.y >= min.y && mouse.y <= max.y;
    return inside ? Handle::Body : Handle::None;
}

// Marks the frame of the selected element for the overlay of the viewport: its outline, its handles
// and its anchors.
void markSelection(ViewportMarks& marks, const CanvasView& view, const ui::LayoutResult& layout, const ui::LaidOutRect& rect,
                   const scene::UiRect& component)
{
    marks.element = std::pair{view.toScreen(rect.min), view.toScreen(rect.max)};
    marks.handleRadius = handleRadius;
    for (const auto& [handle, point] : handlesOf(view, rect))
    {
        static_cast<void>(handle);
        marks.handles.push_back(point);
    }

    // The anchors, as four rings on the canvas: what the element hangs from, and so what it
    // follows when the window changes size.
    const math::Vec2 size = layout.canvasSize;
    const std::array<math::Vec2, 4> anchors{
        math::Vec2{component.anchorMin.x * size.x, component.anchorMin.y * size.y},
        math::Vec2{component.anchorMax.x * size.x, component.anchorMin.y * size.y},
        math::Vec2{component.anchorMax.x * size.x, component.anchorMax.y * size.y},
        math::Vec2{component.anchorMin.x * size.x, component.anchorMax.y * size.y},
    };
    for (const math::Vec2 anchor : anchors)
    {
        marks.anchors.push_back(view.toScreen(anchor));
    }
}

// Applies a drag to the offsets of the element; the drag is recorded once the mouse is let go.
void dragRect(scene::UiRect& rect, Handle handle, math::Vec2 delta)
{
    const bool left = handle == Handle::Left || handle == Handle::TopLeft || handle == Handle::BottomLeft;
    const bool right = handle == Handle::Right || handle == Handle::TopRight || handle == Handle::BottomRight;
    const bool top = handle == Handle::Top || handle == Handle::TopLeft || handle == Handle::TopRight;
    const bool bottom = handle == Handle::Bottom || handle == Handle::BottomLeft || handle == Handle::BottomRight;
    if (handle == Handle::Body)
    {
        rect.offsetMin = rect.offsetMin + delta;
        rect.offsetMax = rect.offsetMax + delta;
        return;
    }
    if (left)
    {
        rect.offsetMin.x += delta.x;
    }
    if (right)
    {
        rect.offsetMax.x += delta.x;
    }
    if (top)
    {
        rect.offsetMin.y += delta.y;
    }
    if (bottom)
    {
        rect.offsetMax.y += delta.y;
    }
}

[[nodiscard]] platform::Cursor cursorOf(Handle handle) noexcept
{
    switch (handle)
    {
    case Handle::Left:
    case Handle::Right:
        return platform::Cursor::ResizeHorizontal;
    case Handle::Top:
    case Handle::Bottom:
        return platform::Cursor::ResizeVertical;
    case Handle::TopLeft:
    case Handle::BottomRight:
        return platform::Cursor::ResizeDiagonalDown;
    case Handle::TopRight:
    case Handle::BottomLeft:
        return platform::Cursor::ResizeDiagonalUp;
    case Handle::Body:
        return platform::Cursor::Hand;
    case Handle::None:
        break;
    }
    return platform::Cursor::Arrow;
}

// Records the offsets a drag changed, already applied, as one undoable step: both corners move
// together, so undoing one alone would leave a rectangle turned inside out.
void recordRectEdit(ToolsState& state, core::Uuid entity, const scene::UiRect& before, const scene::UiRect& after)
{
    std::vector<std::unique_ptr<Command>> commands;
    const auto record = [&](const char* field, const math::Vec2& from, const math::Vec2& to) {
        if (from != to)
        {
            commands.push_back(makeSetFieldCommand(entity, "UiRect", field,
                                                   scene::writeFieldValue(reflection::ValueKind::Vec2, &from),
                                                   scene::writeFieldValue(reflection::ValueKind::Vec2, &to)));
        }
    };
    record("offset_min", before.offsetMin, after.offsetMin);
    record("offset_max", before.offsetMax, after.offsetMax);
    if (std::unique_ptr<Command> composite = makeCompositeCommand(std::move(commands), "Move element"))
    {
        state.history.recordApplied(std::move(composite));
    }
}

// The selected element, where its canvas placed it.
struct PlacedElement
{
    scene::Entity entity;
    const ui::LayoutResult* layout = nullptr;
    const ui::LaidOutRect* rect = nullptr;
};

[[nodiscard]] PlacedElement placedElement(const scene::Scene& scene, std::span<const LaidOutCanvas> canvases, core::Uuid uuid)
{
    const scene::Entity entity = scene.findEntity(uuid);
    if (!entity.isValid() || !scene.has<scene::UiRect>(entity))
    {
        return {};
    }
    for (const auto& [canvas, layout] : canvases)
    {
        static_cast<void>(canvas);
        if (const ui::LaidOutRect* const rect = layout.find(entity))
        {
            return {.entity = entity, .layout = &layout, .rect = rect};
        }
    }
    return {};
}

// The interfaces the 2D screen shows this frame, with where they are drawn; nothing outside it.
[[nodiscard]] std::optional<InterfaceFrame> editedFrame(const ToolsState& state)
{
    if (!state.camera.isTwoD() || state.playState != PlayState::Editing)
    {
        return std::nullopt;
    }
    return state.interfaceFrame;
}

} // namespace

ScreenContent screenContent(bool twoD, const scene::Scene& scene) noexcept
{
    if (twoD == (scene.kind() == scene::SceneKind::TwoD))
    {
        return ScreenContent::Scene;
    }
    return twoD ? ScreenContent::Interfaces : ScreenContent::Nothing;
}

GameFrame gameFrame(const scene::Scene& scene, float aspect)
{
    GameFrame frame;
    math::Vec2 center{0.0f};
    float halfHeight = scene::Camera{}.orthographicSize;
    for (auto [entity, camera] : scene.view<scene::Camera>())
    {
        // A 3D scene edits its interfaces at the origin: its cameras look at another world.
        if (!camera.primary || scene.kind() != scene::SceneKind::TwoD)
        {
            continue;
        }
        if (camera.projection == scene::Projection::Orthographic)
        {
            const scene::WorldTransform* const world = scene.tryGet<scene::WorldTransform>(entity);
            const scene::Transform* const local = scene.tryGet<scene::Transform>(entity);
            const math::Vec3 position = world != nullptr ? math::Vec3(world->matrix[3])
                                        : local != nullptr ? local->position
                                                           : math::Vec3{0.0f};
            center = math::Vec2{position.x, position.y};
            halfHeight = camera.orthographicSize;
            frame.camera = true;
        }
        break;
    }
    const math::Vec2 half{halfHeight * std::max(aspect, 0.01f), halfHeight};
    frame.min = center - half;
    frame.max = center + half;
    return frame;
}

std::optional<InterfaceFrame> interfaceFrame(const GameFrame& frame, const ViewportView& view)
{
    // The top left corner of the frame, and the bottom right one, on the image of the view.
    const std::optional<math::Vec2> topLeft = view.project(math::Vec3{frame.min.x, frame.max.y, 0.0f});
    const std::optional<math::Vec2> bottomRight = view.project(math::Vec3{frame.max.x, frame.min.y, 0.0f});
    if (!topLeft || !bottomRight || bottomRight->y <= topLeft->y || view.size.y < 1.0f)
    {
        return std::nullopt;
    }
    return InterfaceFrame{
        .layoutSize = view.size,
        .offset = *topLeft,
        .scale = (bottomRight->y - topLeft->y) / view.size.y,
    };
}

void fitToScene(EditorCamera& camera, const scene::Scene& scene)
{
    camera.setTwoD(scene.kind() == scene::SceneKind::TwoD);
    // The 2D view opens on what the game shows, with a margin.
    const GameFrame frame = gameFrame(scene, 16.0f / 9.0f);
    camera.setView2D((frame.min + frame.max) * 0.5f, (frame.max.y - frame.min.y) * 0.5f * 1.15f);
}

bool handleInterfaceEditing(ToolsState& state, scene::Scene& scene, bool hovered)
{
    const std::optional<InterfaceFrame> frame = editedFrame(state);
    if (!frame)
    {
        state.interfaceDragEntity = core::Uuid{};
        return false;
    }
    std::vector<LaidOutCanvas> canvases;
    layoutCanvases(scene, frame->layoutSize, canvases);
    const math::Vec2 pointer = state.input.mouse();

    // A drag under way: the element follows the mouse, and the change is recorded once let go.
    if (!state.interfaceDragEntity.isNil())
    {
        const scene::Entity dragged = scene.findEntity(state.interfaceDragEntity);
        scene::UiRect* const component = scene.tryGet<scene::UiRect>(dragged);
        const PlacedElement placed = placedElement(scene, canvases, state.interfaceDragEntity);
        if (component == nullptr || placed.layout == nullptr)
        {
            state.interfaceDragEntity = core::Uuid{};
            return false;
        }
        const float zoom = viewOf(state, *frame, *placed.layout).zoom;
        const math::Vec2 total = state.input.dragDelta(Mouse::Left);
        const Handle handle = static_cast<Handle>(state.interfaceHandle);
        if (state.input.down(Mouse::Left))
        {
            scene::UiRect edited = state.interfaceDragStart;
            dragRect(edited, handle, math::Vec2{total.x / zoom, total.y / zoom});
            *component = edited;
            state.input.cursor = cursorOf(handle);
            return true;
        }
        recordRectEdit(state, state.interfaceDragEntity, state.interfaceDragStart, *component);
        const bool moved = std::abs(total.x) > 2.0f || std::abs(total.y) > 2.0f;
        if (!moved && handle == Handle::Body)
        {
            // Clicking the selection again without moving it reaches what lies under it, which is
            // how the label inside a button is picked.
            if (const core::Uuid under = pickAt(state, scene, *frame, canvases, pointer, state.selection.active()); !under.isNil())
            {
                state.selection.set(under);
            }
        }
        state.interfaceDragEntity = core::Uuid{};
        state.interfaceHandle = 0;
        return true;
    }

    const bool navigating = state.flying || state.orbiting || state.panning;
    if (!hovered || navigating || state.input.alt())
    {
        return false;
    }
    // The handles of the selected element, then the elements under the mouse, come before the
    // gizmo and the world.
    const PlacedElement selected = placedElement(scene, canvases, state.selection.active());
    const Handle handle = selected.rect != nullptr ? handleUnder(viewOf(state, *frame, *selected.layout), *selected.rect, pointer)
                                                   : Handle::None;
    if (handle != Handle::None)
    {
        state.input.cursor = cursorOf(handle);
    }
    if (!state.input.clicked(Mouse::Left))
    {
        return false;
    }
    if (handle != Handle::None && !state.input.ctrl() && !state.input.shift())
    {
        state.interfaceHandle = static_cast<std::uint8_t>(handle);
        state.interfaceDragStart = scene.get<scene::UiRect>(selected.entity);
        state.interfaceDragEntity = state.selection.active();
        return true;
    }
    if (state.hoveredHandle != GizmoHandle::None)
    {
        return false;
    }
    const core::Uuid picked = pickAt(state, scene, *frame, canvases, pointer, state.selection.active());
    if (picked.isNil())
    {
        return false;
    }
    const std::array entities{picked};
    selectEntities(state, entities, state.input.ctrl() ? SelectMode::Toggle : state.input.shift() ? SelectMode::Add : SelectMode::Replace);
    return true;
}

void drawInterfaceOverlay(ToolsState& state, const scene::Scene& scene)
{
    const std::optional<InterfaceFrame> frame = editedFrame(state);
    if (!frame)
    {
        return;
    }
    const math::Vec2 corner(state.viewportOrigin.x, state.viewportOrigin.y);

    std::vector<LaidOutCanvas> canvases;
    layoutCanvases(scene, frame->layoutSize, canvases);
    // The screen of the game, as Godot outlines its viewport, when no camera outlines it.
    if (!canvases.empty() && !gameFrame(scene, frame->layoutSize.x / std::max(frame->layoutSize.y, 1.0f)).camera)
    {
        const math::Vec2 min(corner.x + frame->offset.x / state.pixelsPerPoint, corner.y + frame->offset.y / state.pixelsPerPoint);
        const math::Vec2 max(min.x + frame->layoutSize.x * frame->scale / state.pixelsPerPoint,
                         min.y + frame->layoutSize.y * frame->scale / state.pixelsPerPoint);
        state.viewportMarks.gameFrame = std::pair{min, max};
    }
    const PlacedElement selected = placedElement(scene, canvases, state.selection.active());
    if (selected.rect != nullptr)
    {
        markSelection(state.viewportMarks, viewOf(state, *frame, *selected.layout), *selected.layout, *selected.rect,
                      scene.get<scene::UiRect>(selected.entity));
    }
}

std::optional<std::pair<math::Vec2, math::Vec2>> selectedInterfaceBounds(const ToolsState& state, const scene::Scene& scene)
{
    const std::optional<InterfaceFrame> frame = editedFrame(state);
    if (!frame || state.viewportPixels.height == 0)
    {
        return std::nullopt;
    }
    std::vector<LaidOutCanvas> canvases;
    layoutCanvases(scene, frame->layoutSize, canvases);
    // Pixels of the view become meters of the XY plane around the middle of the 2D view.
    const math::Vec2 size{static_cast<float>(state.viewportPixels.width), static_cast<float>(state.viewportPixels.height)};
    const float metersPerPixel = 2.0f * state.camera.orthographicSize() / size.y;
    const auto toWorld = [&](math::Vec2 pixel) {
        return state.camera.center() + math::Vec2{pixel.x - size.x * 0.5f, size.y * 0.5f - pixel.y} * metersPerPixel;
    };
    std::optional<std::pair<math::Vec2, math::Vec2>> bounds;
    for (const core::Uuid uuid : state.selection.entities())
    {
        const PlacedElement placed = placedElement(scene, canvases, uuid);
        if (placed.rect == nullptr)
        {
            continue;
        }
        const float scale = frame->scale * placed.layout->scale;
        const math::Vec2 first = toWorld(frame->offset + placed.rect->min * scale);
        const math::Vec2 second = toWorld(frame->offset + placed.rect->max * scale);
        const math::Vec2 low = math::min(first, second);
        const math::Vec2 high = math::max(first, second);
        bounds = bounds ? std::pair{math::min(bounds->first, low), math::max(bounds->second, high)} : std::pair{low, high};
    }
    return bounds;
}

} // namespace devex::tools::detail
