#include "ToolsState.hpp"

#include <devex/scene/Scene.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/tools/SceneCommands.hpp>
#include <devex/ui/Layout.hpp>
#include <devex/ui/TextLayout.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace devex::tools::detail {
namespace {

// The handles around the selection, and the body that moves it whole.
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

// Interface colours are linear, as every colour of the engine is, and the panels of the editor are
// drawn in the encoding of the display: the 2D screen converts them so that it shows what the game
// will show.
[[nodiscard]] ImVec4 displayColor(math::Vec4 color, float opacity) noexcept
{
    const auto encode = [](float channel) {
        const float value = std::clamp(channel, 0.0f, 1.0f);
        return value <= 0.0031308f ? value * 12.92f
                                   : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
    };
    return ImVec4(encode(color.x), encode(color.y), encode(color.z),
                  std::clamp(color.w * opacity, 0.0f, 1.0f));
}

// Where a point of the canvas lands on the screen.
struct CanvasView
{
    ImVec2 origin{0.0f, 0.0f};
    float zoom = 1.0f;

    [[nodiscard]] ImVec2 toScreen(math::Vec2 point) const noexcept
    {
        return ImVec2(origin.x + point.x * zoom, origin.y + point.y * zoom);
    }

    [[nodiscard]] math::Vec2 toCanvas(ImVec2 point) const noexcept
    {
        return math::Vec2{(point.x - origin.x) / zoom, (point.y - origin.y) / zoom};
    }
};

// The canvases of the scene, laid out at the resolution the panel shows.
void layoutCanvases(const scene::Scene& scene, math::Vec2 size,
                    std::vector<std::pair<scene::Entity, ui::LayoutResult>>& canvases)
{
    canvases.clear();
    for (auto [entity, canvas] : scene.view<scene::Canvas>())
    {
        static_cast<void>(canvas);
        ui::LayoutResult layout;
        // The panel shows the canvas at its own size, whatever the window it would run in.
        ui::layoutCanvas(scene, entity, size, layout);
        canvases.emplace_back(entity, std::move(layout));
    }
    std::ranges::stable_sort(canvases, [&scene](const auto& first, const auto& second) {
        return scene.get<scene::Canvas>(first.first).sortOrder <
               scene.get<scene::Canvas>(second.first).sortOrder;
    });
}

// The size the panel lays the interface out at: the one the first canvas was designed for.
[[nodiscard]] math::Vec2 referenceResolution(const scene::Scene& scene)
{
    for (auto [entity, canvas] : scene.view<scene::Canvas>())
    {
        static_cast<void>(entity);
        if (canvas.referenceResolution.x > 1.0f && canvas.referenceResolution.y > 1.0f)
        {
            return canvas.referenceResolution;
        }
    }
    return math::Vec2{1920.0f, 1080.0f};
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
[[nodiscard]] core::Uuid pickAt(const scene::Scene& scene,
                                std::span<const std::pair<scene::Entity, ui::LayoutResult>> canvases,
                                math::Vec2 point, core::Uuid selected)
{
    // The elements under the point, the one drawn last first.
    std::vector<scene::Entity> hits;
    for (auto canvas = canvases.rbegin(); canvas != canvases.rend(); ++canvas)
    {
        for (auto rect = canvas->second.rects.rbegin(); rect != canvas->second.rects.rend(); ++rect)
        {
            if (rect->visible && rect->opacity > 0.0f && contains(*rect, point))
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

// Draws one element the way it will appear: its colour, its texture as a checker, and its text.
void drawElement(ImDrawList& drawing, const CanvasView& view, const scene::Scene& scene,
                 const ui::LaidOutRect& rect)
{
    const ImVec2 min = view.toScreen(rect.min);
    const ImVec2 max = view.toScreen(rect.max);
    if (const scene::UiImage* const image = scene.tryGet<scene::UiImage>(rect.entity))
    {
        const ImVec4 color = displayColor(image->color, rect.opacity);
        drawing.AddRectFilled(min, max, ImGui::GetColorU32(color), image->cornerRadius * view.zoom);
        if (image->texture.isValid())
        {
            // The picture itself is drawn by the renderer; the panel shows where it sits.
            drawing.AddRect(min, max, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.35f * rect.opacity)),
                            image->cornerRadius * view.zoom);
        }
    }
    if (const scene::UiText* const text = scene.tryGet<scene::UiText>(rect.entity))
    {
        const ImVec4 color = displayColor(text->color, rect.opacity);
        const float size = std::max(text->size * view.zoom, 6.0f);
        const ImVec2 measured = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text->text.c_str());
        ImVec2 at = min;
        switch (text->align)
        {
        case scene::TextAlign::Center:
            at.x += (max.x - min.x - measured.x) * 0.5f;
            break;
        case scene::TextAlign::Right:
            at.x += max.x - min.x - measured.x;
            break;
        case scene::TextAlign::Left:
            break;
        }
        switch (text->verticalAlign)
        {
        case scene::TextVerticalAlign::Middle:
            at.y += (max.y - min.y - measured.y) * 0.5f;
            break;
        case scene::TextVerticalAlign::Bottom:
            at.y += max.y - min.y - measured.y;
            break;
        case scene::TextVerticalAlign::Top:
            break;
        }
        // The panel writes with the font of the editor: the font of the game is drawn by the
        // renderer, in the viewport and in the game.
        drawing.AddText(ImGui::GetFont(), size, at, ImGui::GetColorU32(color), text->text.c_str());
    }
}

// The eight handles of a rectangle, and the middle of each side.
[[nodiscard]] std::array<std::pair<Handle, ImVec2>, 8> handlesOf(const CanvasView& view,
                                                                 const ui::LaidOutRect& rect)
{
    const ImVec2 min = view.toScreen(rect.min);
    const ImVec2 max = view.toScreen(rect.max);
    const ImVec2 middle((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    return {{
        {Handle::TopLeft, min},
        {Handle::Top, ImVec2(middle.x, min.y)},
        {Handle::TopRight, ImVec2(max.x, min.y)},
        {Handle::Right, ImVec2(max.x, middle.y)},
        {Handle::BottomRight, max},
        {Handle::Bottom, ImVec2(middle.x, max.y)},
        {Handle::BottomLeft, ImVec2(min.x, max.y)},
        {Handle::Left, ImVec2(min.x, middle.y)},
    }};
}

[[nodiscard]] Handle handleUnder(const CanvasView& view, const ui::LaidOutRect& rect, ImVec2 mouse)
{
    for (const auto& [handle, point] : handlesOf(view, rect))
    {
        if (std::abs(mouse.x - point.x) <= handleRadius + 2.0f &&
            std::abs(mouse.y - point.y) <= handleRadius + 2.0f)
        {
            return handle;
        }
    }
    const ImVec2 min = view.toScreen(rect.min);
    const ImVec2 max = view.toScreen(rect.max);
    const bool inside = mouse.x >= min.x && mouse.x <= max.x && mouse.y >= min.y && mouse.y <= max.y;
    return inside ? Handle::Body : Handle::None;
}

// Draws the frame of the selected element: its outline, its handles and its anchors.
void drawSelection(ImDrawList& drawing, const CanvasView& view, const ui::LayoutResult& layout,
                   const ui::LaidOutRect& rect, const scene::UiRect& component,
                   const ThemeColors& colors)
{
    const ImU32 accent = uiColorU32(colors.accent);
    drawing.AddRect(view.toScreen(rect.min), view.toScreen(rect.max), accent, 0.0f, 0, 1.5f);
    for (const auto& [handle, point] : handlesOf(view, rect))
    {
        static_cast<void>(handle);
        drawing.AddRectFilled(ImVec2(point.x - handleRadius, point.y - handleRadius),
                              ImVec2(point.x + handleRadius, point.y + handleRadius), accent, 2.0f);
    }

    // The anchors, as four rings on the canvas: what the element hangs from, and so what it
    // follows when the window changes size.
    const ImU32 anchorColor = uiColorU32(colors.textDim);
    const math::Vec2 size = layout.canvasSize;
    const std::array<math::Vec2, 4> anchors{
        math::Vec2{component.anchorMin.x * size.x, component.anchorMin.y * size.y},
        math::Vec2{component.anchorMax.x * size.x, component.anchorMin.y * size.y},
        math::Vec2{component.anchorMax.x * size.x, component.anchorMax.y * size.y},
        math::Vec2{component.anchorMin.x * size.x, component.anchorMax.y * size.y},
    };
    for (const math::Vec2 anchor : anchors)
    {
        const ImVec2 point = view.toScreen(anchor);
        drawing.AddCircle(point, 4.0f, anchorColor, 0, 1.5f);
    }
}

// Applies a drag to the offsets of the element, and records it once the mouse is let go.
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

[[nodiscard]] ImGuiMouseCursor cursorOf(Handle handle) noexcept
{
    switch (handle)
    {
    case Handle::Left:
    case Handle::Right:
        return ImGuiMouseCursor_ResizeEW;
    case Handle::Top:
    case Handle::Bottom:
        return ImGuiMouseCursor_ResizeNS;
    case Handle::TopLeft:
    case Handle::BottomRight:
        return ImGuiMouseCursor_ResizeNWSE;
    case Handle::TopRight:
    case Handle::BottomLeft:
        return ImGuiMouseCursor_ResizeNESW;
    case Handle::Body:
        return ImGuiMouseCursor_Hand;
    case Handle::None:
        break;
    }
    return ImGuiMouseCursor_Arrow;
}

// Records the offsets a drag changed, already applied, as one undoable step: both corners move
// together, so undoing one alone would leave a rectangle turned inside out.
void recordRectEdit(ToolsState& state, core::Uuid entity, const scene::UiRect& before,
                    const scene::UiRect& after)
{
    std::vector<std::unique_ptr<Command>> commands;
    const auto record = [&](const char* field, const math::Vec2& from, const math::Vec2& to) {
        if (from != to)
        {
            commands.push_back(makeSetFieldCommand(
                entity, "UiRect", field, scene::writeFieldValue(reflection::ValueKind::Vec2, &from),
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

void drawToolbar(ToolsState& state, const scene::Scene& scene, std::size_t canvasCount)
{
    const ThemeColors& colors = themeColors();
    ImGui::AlignTextToFramePadding();
    iconLabel(icons::Square, colors.textDim);
    if (canvasCount == 0)
    {
        ImGui::TextDisabled("No canvas in the scene: add one to an entity to build an interface");
        return;
    }
    ImGui::TextDisabled("%zu canvas%s", canvasCount, canvasCount == 1 ? "" : "es");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("##zoom", &state.interfaceZoom, 0.1f, 2.0f, "Zoom %.2fx",
                       ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
    ImGui::SameLine();
    if (const scene::Entity selected = scene.findEntity(state.selection);
        selected.isValid() && scene.tryGet<scene::UiRect>(selected) != nullptr)
    {
        ImGui::TextDisabled("%s", scene.name(selected).c_str());
        ImGui::SetItemTooltip("Drag the element to move it, its handles to resize it");
    }
    else
    {
        ImGui::TextDisabled("Select an element to move it");
    }
}

} // namespace

void drawInterfacePanel(ToolsState& state, scene::Scene& scene)
{
    const bool open = ImGui::Begin(interfaceWindow, nullptr,
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoScrollWithMouse);
    if (!open)
    {
        ImGui::End();
        return;
    }

    std::vector<std::pair<scene::Entity, ui::LayoutResult>> canvases;
    // Interfaces are edited at the reference resolution of their canvas, as they were designed.
    const math::Vec2 reference = referenceResolution(scene);
    layoutCanvases(scene, reference, canvases);
    drawToolbar(state, scene, canvases.size());

    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (available.x < 16.0f || available.y < 16.0f)
    {
        ImGui::End();
        return;
    }
    const ImVec2 corner = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##canvas", available, ImGuiButtonFlags_MouseButtonLeft);
    const bool hovered = ImGui::IsItemHovered();

    // The canvas is shown whole, centred, at the zoom the toolbar chose.
    const float fit = std::min(available.x / reference.x, available.y / reference.y);
    const CanvasView view{
        .origin = ImVec2(corner.x + (available.x - reference.x * fit * state.interfaceZoom) * 0.5f,
                         corner.y + (available.y - reference.y * fit * state.interfaceZoom) * 0.5f),
        .zoom = fit * state.interfaceZoom,
    };

    const ThemeColors& colors = themeColors();
    ImDrawList& drawing = *ImGui::GetWindowDrawList();
    drawing.PushClipRect(corner, ImVec2(corner.x + available.x, corner.y + available.y), true);
    // The screen the interface is designed for.
    drawing.AddRectFilled(view.toScreen(math::Vec2{0.0f, 0.0f}), view.toScreen(reference),
                          uiColorU32(colors.outer));
    drawing.AddRect(view.toScreen(math::Vec2{0.0f, 0.0f}), view.toScreen(reference),
                    uiColorU32(colors.textDim));

    for (const auto& [canvasEntity, layout] : canvases)
    {
        static_cast<void>(canvasEntity);
        for (const ui::LaidOutRect& rect : layout.rects)
        {
            if (rect.visible && rect.opacity > 0.0f)
            {
                drawElement(drawing, view, scene, rect);
            }
        }
    }

    // The element under the mouse, topmost first, and the one that is selected.
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const scene::Entity selected = scene.findEntity(state.selection);
    const ui::LaidOutRect* selectedRect = nullptr;
    const ui::LayoutResult* selectedLayout = nullptr;
    for (const auto& [canvasEntity, layout] : canvases)
    {
        static_cast<void>(canvasEntity);
        if (const ui::LaidOutRect* const found = layout.find(selected))
        {
            selectedRect = found;
            selectedLayout = &layout;
        }
    }

    Handle handle = Handle::None;
    if (hovered && selectedRect != nullptr)
    {
        handle = handleUnder(view, *selectedRect, mouse);
        if (handle != Handle::None)
        {
            ImGui::SetMouseCursor(cursorOf(handle));
        }
    }

    if (ImGui::IsItemActivated())
    {
        state.interfaceHandle = static_cast<std::uint8_t>(handle);
        if (selectedRect != nullptr && handle != Handle::None)
        {
            state.interfaceDragStart = scene.get<scene::UiRect>(selected);
            state.interfaceDragEntity = state.selection;
        }
        else
        {
            // Clicking elsewhere selects what lies there.
            state.interfaceDragEntity = core::Uuid{};
            state.selection = pickAt(scene, canvases, view.toCanvas(mouse), state.selection);
        }
    }

    if (ImGui::IsItemActive() && !state.interfaceDragEntity.isNil())
    {
        const scene::Entity dragged = scene.findEntity(state.interfaceDragEntity);
        if (scene::UiRect* const component = scene.tryGet<scene::UiRect>(dragged))
        {
            const ImVec2 total = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
            scene::UiRect edited = state.interfaceDragStart;
            dragRect(edited, static_cast<Handle>(state.interfaceHandle),
                     math::Vec2{total.x / view.zoom, total.y / view.zoom});
            *component = edited;
        }
    }
    if (ImGui::IsItemDeactivated() && !state.interfaceDragEntity.isNil())
    {
        const ImVec2 total = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        const bool moved = std::abs(total.x) > 2.0f || std::abs(total.y) > 2.0f;
        const scene::Entity dragged = scene.findEntity(state.interfaceDragEntity);
        if (const scene::UiRect* const component = scene.tryGet<scene::UiRect>(dragged))
        {
            recordRectEdit(state, state.interfaceDragEntity, state.interfaceDragStart, *component);
        }
        if (!moved && static_cast<Handle>(state.interfaceHandle) == Handle::Body)
        {
            // Clicking the selection again without moving it reaches what lies under it, which is
            // how the label inside a button is picked.
            state.selection = pickAt(scene, canvases, view.toCanvas(mouse), state.selection);
        }
        state.interfaceDragEntity = core::Uuid{};
        state.interfaceHandle = 0;
    }

    if (selectedRect != nullptr && selectedLayout != nullptr)
    {
        drawSelection(drawing, view, *selectedLayout, *selectedRect,
                      scene.get<scene::UiRect>(selected), colors);
    }
    drawing.PopClipRect();
    ImGui::End();
}

} // namespace devex::tools::detail
