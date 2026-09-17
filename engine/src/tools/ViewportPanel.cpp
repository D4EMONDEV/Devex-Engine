#include "ToolsState.hpp"

#include <devex/asset/Project.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/platform/Input.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <utility>

namespace devex::tools::detail {
namespace {

using platform::Key;
using platform::MouseButton;

// Icons of entities without a mesh are selected within this distance of their position.
constexpr float iconPickRadius = 14.0f;
// A press that moves less than this is a click.
constexpr float clickTolerance = 4.0f;

[[nodiscard]] math::Mat4 worldMatrixOf(const scene::Scene& scene, scene::Entity entity)
{
    const scene::WorldTransform* const world = entity.isValid() ? scene.tryGet<scene::WorldTransform>(entity) : nullptr;
    return world != nullptr ? world->matrix : math::Mat4{1.0f};
}

// The light or camera whose icon is under the mouse, or an invalid entity.
[[nodiscard]] scene::Entity iconAt(scene::Scene& scene, const ViewportView& view, math::Vec2 mouse, float pixelsPerPoint)
{
    scene::Entity closest;
    float closestDistance = iconPickRadius * pixelsPerPoint;
    const auto consider = [&](scene::Entity entity, const scene::WorldTransform& world) {
        const std::optional<math::Vec2> pixel = view.project(math::Vec3(world.matrix[3]));
        if (const float distance = pixel ? math::length(*pixel - mouse) : std::numeric_limits<float>::max();
            distance < closestDistance)
        {
            closest = entity;
            closestDistance = distance;
        }
    };
    for ([[maybe_unused]] auto [entity, world, light] : scene.view<scene::WorldTransform, scene::DirectionalLight>())
    {
        consider(entity, world);
    }
    for ([[maybe_unused]] auto [entity, world, light] : scene.view<scene::WorldTransform, scene::PointLight>())
    {
        consider(entity, world);
    }
    for ([[maybe_unused]] auto [entity, world, light] : scene.view<scene::WorldTransform, scene::SpotLight>())
    {
        consider(entity, world);
    }
    for ([[maybe_unused]] auto [entity, world, camera] : scene.view<scene::WorldTransform, scene::Camera>())
    {
        consider(entity, world);
    }
    return closest;
}

// Where a dropped model goes: on the ground under the mouse, or in front of the camera.
[[nodiscard]] math::Vec3 dropPosition(const ToolsState& state, const ViewportView& view, math::Vec2 mouse)
{
    const Ray ray = view.ray(mouse);
    if (const std::optional<math::Vec3> ground = intersectPlane(ray, math::Vec3{0.0f}, math::Vec3{0.0f, 1.0f, 0.0f});
        ground && math::length(*ground - ray.origin) < 500.0f)
    {
        return *ground;
    }
    return state.camera.position() + state.camera.forward() * 8.0f;
}

// The scene tabs above the viewport, with a button to add a scene.
void drawSceneTabs(ToolsState& state, scene::Scene& scene)
{
    const bool editing = state.playState == PlayState::Editing;
    const ActiveDocument live = activeDocument(state, scene);
    const ImGuiTabBarFlags flags = ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll |
                                   ImGuiTabBarFlags_NoCloseWithMiddleMouseButton;
    if (!ImGui::BeginTabBar("scene tabs", flags))
    {
        return;
    }
    // ImGui shows the tab selected on the previous frame: the active tab is selected every frame, and
    // only clicks change it, so that a tab opened from elsewhere is not switched back.
    std::optional<std::size_t> clicked;
    std::optional<std::uint64_t> closed;
    for (std::size_t index = 0; index < state.tabs.size(); ++index)
    {
        const bool active = index == state.tabs.active();
        const std::string label = std::format("{}  {}###tab{}", std::string_view(icons::Clapperboard),
                                              tabName(state.tabs.path(index, live)), state.tabs.id(index));
        ImGuiTabItemFlags itemFlags = ImGuiTabItemFlags_None;
        if (state.tabs.isModified(index, live) && (editing || !active))
        {
            itemFlags |= ImGuiTabItemFlags_UnsavedDocument;
        }
        if (active)
        {
            itemFlags |= ImGuiTabItemFlags_SetSelected;
        }
        bool open = true;
        const bool visible = ImGui::BeginTabItem(label.c_str(), editing ? &open : nullptr, itemFlags);
        if (!active && ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            clicked = index;
        }
        if (visible)
        {
            ImGui::EndTabItem();
        }
        if (const std::filesystem::path& path = state.tabs.path(index, live); !path.empty())
        {
            ImGui::SetItemTooltip("%s", core::toUtf8(path).c_str());
        }
        if (!open)
        {
            closed = state.tabs.id(index);
        }
    }
    if (editing && ImGui::TabItemButton(icons::Plus.c_str(), ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
    {
        newSceneTab(state, scene);
    }
    ImGui::EndTabBar();

    if (clicked && editing)
    {
        activateSceneTab(state, scene, *clicked);
    }
    if (closed)
    {
        requestAction(state, scene, {.kind = PendingAction::Kind::CloseTab, .tab = *closed});
    }
}

void drawToolbar(ToolsState& state, scene::Scene& scene)
{
    const ThemeColors& colors = themeColors();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + style.ItemSpacing.x);
    const auto toolButtonFor = [&](const char* id, IconText icon, EditorTool tool, const char* tooltip) {
        if (toolButton(id, icon, tooltip, state.tool == tool))
        {
            state.tool = tool;
        }
        ImGui::SameLine(0.0f, 2.0f);
    };
    toolButtonFor("select", icons::Pointer, EditorTool::Select, "Select (Q)");
    toolButtonFor("move", icons::Move, EditorTool::Move, "Move (W)");
    toolButtonFor("rotate", icons::Rotate, EditorTool::Rotate, "Rotate (E)");
    toolButtonFor("scale", icons::Scale, EditorTool::Scale, "Scale (R)");
    toolbarSeparator();
    const bool local = state.gizmo.space == GizmoSpace::Local;
    if (toolButton("space", local ? icons::Box : icons::Globe,
                   local ? "Handles follow the entity's axes (X). Scaling is always local."
                         : "Handles follow the world axes (X). Scaling is always local.",
                   local))
    {
        state.gizmo.space = local ? GizmoSpace::World : GizmoSpace::Local;
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (toolButton("snap", icons::Magnet, "Snap by 0.5 m, 15° or 0.1 (Ctrl switches it while dragging)", state.snap))
    {
        state.snap = !state.snap;
    }
    toolbarSeparator();
    if (toolButton("grid", icons::Grid, "Grid", state.showGrid))
    {
        state.showGrid = !state.showGrid;
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (toolButton("icons", icons::Eye, "Light and camera icons", state.showIcons))
    {
        state.showIcons = !state.showIcons;
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (toolButton("colliders", icons::Scan, "Collision shapes of every entity (the selection always shows its own)",
                   state.showColliders))
    {
        state.showColliders = !state.showColliders;
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (toolButton("frame", icons::Crosshair, "Frame the selection (F)", false, scene.findEntity(state.selection).isValid()))
    {
        frameSelection(state, scene);
    }

    const std::string speed = std::format("{:.1f} m/s", state.camera.speed());
    const std::string exposure = std::format("EV {:.1f}", state.renderer.stats().ev100);
    const float rightWidth = ImGui::CalcTextSize(icons::Gauge.c_str()).x + ImGui::CalcTextSize(speed.c_str()).x +
                             ImGui::CalcTextSize(exposure.c_str()).x + toolButtonWidth() + style.ItemSpacing.x * 4.0f +
                             style.ItemInnerSpacing.x;
    ImGui::SameLine();
    alignRight(rightWidth);
    ImGui::AlignTextToFramePadding();
    iconLabel(icons::Gauge, colors.textDim);
    ImGui::TextDisabled("%s", speed.c_str());
    ImGui::SetItemTooltip("Flying speed: the mouse wheel changes it while flying");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", exposure.c_str());
    ImGui::SetItemTooltip("Exposure of the editor camera");
    ImGui::SameLine();
    toolButton("help", icons::CircleHelp,
               "Right drag: look, with W A S D to fly, Q E to go down and up, Shift to go faster\n"
               "Alt + left drag: orbit    Middle drag: pan    Wheel: move forward\n"
               "F: frame the selection    Delete: delete it    Ctrl: snap");
}

// Records the fields a drag changed, already applied, as undoable steps.
void recordTransformEdit(ToolsState& state, core::Uuid entity, const scene::Transform& before, const scene::Transform& after)
{
    const auto record = [&](const char* field, reflection::ValueKind kind, const void* from, const void* to) {
        state.history.recordApplied(makeSetFieldCommand(entity, "Transform", field, scene::writeFieldValue(kind, from),
                                                        scene::writeFieldValue(kind, to)));
    };
    if (before.position != after.position)
    {
        record("position", reflection::ValueKind::Vec3, &before.position, &after.position);
    }
    if (before.rotation != after.rotation)
    {
        record("rotation", reflection::ValueKind::Quat, &before.rotation, &after.rotation);
    }
    if (before.scale != after.scale)
    {
        record("scale", reflection::ValueKind::Vec3, &before.scale, &after.scale);
    }
}

void handleCamera(ToolsState& state, bool hovered, math::Vec2 size)
{
    const ImGuiIO& io = ImGui::GetIO();
    const platform::Input& input = state.platform.input();

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        state.flying = true;
        state.window.setMouseCaptured(true);
        ImGui::SetWindowFocus();
    }
    if (state.flying)
    {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && !input.isMouseButtonDown(MouseButton::Right))
        {
            state.flying = false;
            state.window.setMouseCaptured(false);
        }
        else
        {
            state.camera.look(input.mouseDelta());
            const auto axis = [&](Key positive, Key negative) {
                return (input.isKeyDown(positive) ? 1.0f : 0.0f) - (input.isKeyDown(negative) ? 1.0f : 0.0f);
            };
            state.camera.fly(math::Vec3{axis(Key::D, Key::A), axis(Key::E, Key::Q), axis(Key::S, Key::W)}, io.DeltaTime,
                             input.isKeyDown(Key::LeftShift) || input.isKeyDown(Key::RightShift));
            if (io.MouseWheel != 0.0f)
            {
                state.camera.changeSpeed(io.MouseWheel);
            }
        }
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
    {
        state.panning = true;
    }
    if (state.panning)
    {
        state.panning = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        state.camera.pan(math::Vec2(io.MouseDelta.x, io.MouseDelta.y) * state.pixelsPerPoint, size.y);
    }

    if (hovered && io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        state.orbiting = true;
    }
    if (state.orbiting)
    {
        state.orbiting = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        state.camera.orbit(math::Vec2(io.MouseDelta.x, io.MouseDelta.y));
    }

    if (hovered && !state.flying && io.MouseWheel != 0.0f)
    {
        state.camera.dolly(io.MouseWheel);
    }
}

void handleGizmoAndSelection(ToolsState& state, scene::Scene& scene, const ViewportView& view, math::Vec2 mouse,
                             bool hovered)
{
    if (!state.gizmo.isDragging())
    {
        state.gizmo.mode = state.tool == EditorTool::Rotate ? GizmoMode::Rotate
                           : state.tool == EditorTool::Scale ? GizmoMode::Scale
                                                             : GizmoMode::Translate;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const bool navigating = state.flying || state.orbiting || state.panning;
    const scene::Entity selected = scene.findEntity(state.selection);
    scene::Transform* const local = selected.isValid() ? scene.tryGet<scene::Transform>(selected) : nullptr;

    if (local != nullptr && scene.has<scene::WorldTransform>(selected) && !navigating && state.tool != EditorTool::Select)
    {
        const math::Mat4 world = worldMatrixOf(scene, selected);
        const math::Mat4 parentWorld = worldMatrixOf(scene, scene.parent(selected));
        if (!state.gizmo.isDragging())
        {
            state.hoveredHandle = hovered ? state.gizmo.hitTest(view, world, mouse) : GizmoHandle::None;
            if (hovered && !io.KeyAlt && state.hoveredHandle != GizmoHandle::None && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                state.gizmo.begin(state.hoveredHandle, view, world, parentWorld, *local, mouse);
            }
        }
        if (state.gizmo.isDragging())
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                *local = state.gizmo.drag(view, mouse, io.KeyCtrl != state.snap);
            }
            else
            {
                recordTransformEdit(state, state.selection, state.gizmo.startTransform(), *local);
                state.gizmo.end();
            }
            return;
        }
    }
    else
    {
        state.hoveredHandle = GizmoHandle::None;
        if (state.gizmo.isDragging())
        {
            // The entity disappeared while dragging, or navigation took over.
            if (local != nullptr)
            {
                recordTransformEdit(state, state.selection, state.gizmo.startTransform(), *local);
            }
            state.gizmo.end();
        }
    }

    if (hovered && !io.KeyAlt && !navigating && state.hoveredHandle == GizmoHandle::None &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        state.clickStart = mouse;
    }
    if (state.clickStart && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        if (math::length(mouse - *state.clickStart) <= clickTolerance * state.pixelsPerPoint)
        {
            if (const scene::Entity icon = iconAt(scene, view, mouse, state.pixelsPerPoint); icon.isValid())
            {
                state.selection = scene.uuid(icon);
            }
            else
            {
                state.pickPixel = mouse;
            }
        }
        state.clickStart.reset();
    }
}

void handleKeys(ToolsState& state, scene::Scene& scene)
{
    if (state.flying || ImGui::GetIO().WantTextInput)
    {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Q, false))
    {
        state.tool = EditorTool::Select;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_W, false))
    {
        state.tool = EditorTool::Move;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_E, false))
    {
        state.tool = EditorTool::Rotate;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_R, false))
    {
        state.tool = EditorTool::Scale;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_X, false))
    {
        state.gizmo.space = state.gizmo.space == GizmoSpace::World ? GizmoSpace::Local : GizmoSpace::World;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F, false))
    {
        frameSelection(state, scene);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && scene.findEntity(state.selection).isValid())
    {
        state.pendingCommand = makeDestroyEntityCommand(state.selection);
    }
}

} // namespace

void frameSelection(ToolsState& state, const scene::Scene& scene)
{
    const scene::Entity entity = scene.findEntity(state.selection);
    if (!entity.isValid())
    {
        return;
    }
    const math::Trs world = math::decomposeTrs(worldMatrixOf(scene, entity));
    const float largest = std::max({std::abs(world.scale.x), std::abs(world.scale.y), std::abs(world.scale.z)});
    // Built-in meshes span one unit; hierarchies such as models are usually larger.
    const float radius = scene.has<scene::MeshRenderer>(entity) ? largest * 0.87f
                         : scene.firstChild(entity).isValid() ? std::max(largest, 1.0f) * 2.0f
                                                              : 1.0f;
    state.camera.frame(world.translation, radius);
}

void drawViewportPanel(ToolsState& state, scene::Scene& scene)
{
    state.viewportHovered = false;
    state.viewportFocused = false;
    if (!state.showViewport)
    {
        state.viewportPixels = {};
        return;
    }

    if (std::exchange(state.focusViewport, false))
    {
        ImGui::SetNextWindowFocus();
    }
    // The scene tabs take the place of the panel's own tab.
    ImGuiWindowClass windowClass;
    windowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar;
    ImGui::SetNextWindowClass(&windowClass);
    const ThemeColors& colors = themeColors();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, uiColor(colors.outer));
    const bool open = ImGui::Begin(viewportWindow, nullptr,
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (!open)
    {
        state.viewportPixels = {};
        ImGui::End();
        return;
    }

    const bool editing = state.playState == PlayState::Editing;
    drawSceneTabs(state, scene);
    // The toolbar sits on the panel color, under the tabs.
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float height = ImGui::GetFrameHeight() + style.FramePadding.y * 2.0f;
        ImGui::GetWindowDrawList()->AddRectFilled(start, ImVec2(start.x + ImGui::GetContentRegionAvail().x, start.y + height),
                                                  uiColorU32(colors.panel));
        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + style.FramePadding.y));
        if (editing)
        {
            drawToolbar(state, scene);
        }
        else
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + style.ItemSpacing.x);
            ImGui::AlignTextToFramePadding();
            iconLabel(state.playState == PlayState::Paused ? icons::Pause : icons::Play, colors.accent);
            ImGui::TextColored(uiColor(colors.accent), "%s",
                               state.playState == PlayState::Paused ? "Paused" : "Playing: click the view to give the game the keyboard");
        }
        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + height));
    }

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    state.pixelsPerPoint = io.DisplayFramebufferScale.x > 0.0f ? io.DisplayFramebufferScale.x : 1.0f;
    const auto width = static_cast<std::uint32_t>(std::max(0.0f, std::floor(available.x * state.pixelsPerPoint)));
    const auto height = static_cast<std::uint32_t>(std::max(0.0f, std::floor(available.y * state.pixelsPerPoint)));
    if (width < 8 || height < 8)
    {
        state.viewportPixels = {};
        ImGui::End();
        return;
    }
    state.viewportPixels = {width, height};
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    state.viewportOrigin = {origin.x, origin.y};

    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(state.renderer.viewportTexture())), available);
    const bool hovered = ImGui::IsItemHovered();
    state.viewportHovered = hovered;
    state.viewportFocused = ImGui::IsWindowFocused();
    if (!editing)
    {
        // The game is framed in the accent color while it runs.
        ImGui::GetWindowDrawList()->AddRect(origin, origin + available, uiColorU32(colors.accent), 0.0f, 0, 2.0f);
    }

    const math::Vec2 size{static_cast<float>(width), static_cast<float>(height)};
    const math::Vec2 mouse = (math::Vec2(io.MousePos.x, io.MousePos.y) - state.viewportOrigin) * state.pixelsPerPoint;
    const ViewportView view{.view = state.camera.view(), .verticalFov = EditorCamera::verticalFov, .size = size};

    if (editing)
    {
        if (const std::optional<asset::AssetId> model = acceptDroppedAsset(asset::AssetType::Model))
        {
            requestInstantiateModel(state, *model, core::Uuid{}, dropPosition(state, view, mouse));
        }
        if (const std::optional<asset::AssetId> dropped = acceptDroppedAsset(asset::AssetType::Scene);
            dropped && state.database != nullptr)
        {
            const std::optional<asset::SourceFile> source = state.database->sourceOf(*dropped);
            if (const std::optional<std::filesystem::path> path =
                    source ? state.database->project().absolutePath(source->path) : std::nullopt)
            {
                openSceneTab(state, scene, *path);
            }
        }
        if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
        {
            ImGui::SetWindowFocus();
        }
        handleCamera(state, hovered, size);
        handleGizmoAndSelection(state, scene, view, mouse, hovered);
        if (state.viewportFocused || hovered)
        {
            handleKeys(state, scene);
        }
    }
    else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        // Clicking the game gives it the keyboard.
        ImGui::SetWindowFocus();
    }
    ImGui::End();
}

} // namespace devex::tools::detail
