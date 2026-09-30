#include "EditorFrame.hpp"
#include "SettingsUi.hpp"
#include "ToolsState.hpp"
#include "TwoDScreen.hpp"

#include <devex/core/Profiler.hpp>
#include <devex/asset/Project.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/platform/Input.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cstring>
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

// Calls the function with the entities that show an icon in the viewport, and where.
template <typename Function>
void forEachIcon(const ToolsState& state, scene::Scene& scene, Function&& function)
{
    const auto consider = [&](scene::Entity entity, const scene::WorldTransform& world) {
        if (!isHidden(state, scene, entity))
        {
            function(entity, math::Vec3(world.matrix[3]));
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
}

// The light or camera whose icon is under the mouse, or an invalid entity.
[[nodiscard]] scene::Entity iconAt(const ToolsState& state, scene::Scene& scene, const ViewportView& view, math::Vec2 mouse)
{
    scene::Entity closest;
    float closestDistance = iconPickRadius * state.pixelsPerPoint;
    forEachIcon(state, scene, [&](scene::Entity entity, math::Vec3 position) {
        const std::optional<math::Vec2> pixel = view.project(position);
        if (const float distance = pixel ? math::length(*pixel - mouse) : std::numeric_limits<float>::max();
            distance < closestDistance)
        {
            closest = entity;
            closestDistance = distance;
        }
    });
    return closest;
}

// The lights and cameras whose icons are inside a rectangle of viewport pixels.
[[nodiscard]] std::vector<core::Uuid> iconsIn(const ToolsState& state, scene::Scene& scene, const ViewportView& view,
                                              math::Vec2 min, math::Vec2 max)
{
    std::vector<core::Uuid> found;
    forEachIcon(state, scene, [&](scene::Entity entity, math::Vec3 position) {
        if (const std::optional<math::Vec2> pixel = view.project(position);
            pixel && pixel->x >= min.x && pixel->x <= max.x && pixel->y >= min.y && pixel->y <= max.y)
        {
            found.push_back(scene.uuid(outermostTarget(scene, entity)));
        }
    });
    return found;
}

// How the modifier keys change what a click selects.
[[nodiscard]] SelectMode selectMode(const ToolsState& state)
{
    return state.input.ctrl() ? SelectMode::Toggle : state.input.shift() ? SelectMode::Add : SelectMode::Replace;
}

// Where a dropped model goes: on the ground under the mouse, or in front of the camera.
[[nodiscard]] math::Vec3 dropPosition(const ToolsState& state, const ViewportView& view, math::Vec2 mouse)
{
    const Ray ray = view.ray(mouse);
    // The 2D view places things on the XY plane, under the mouse.
    if (state.camera.isTwoD())
    {
        const std::optional<math::Vec3> plane = intersectPlane(ray, math::Vec3{0.0f}, math::Vec3{0.0f, 0.0f, 1.0f});
        return plane.value_or(math::Vec3{state.camera.pivot().x, state.camera.pivot().y, 0.0f});
    }
    if (const std::optional<math::Vec3> ground = intersectPlane(ray, math::Vec3{0.0f}, math::Vec3{0.0f, 1.0f, 0.0f});
        ground && math::length(*ground - ray.origin) < 500.0f)
    {
        return *ground;
    }
    return state.camera.position() + state.camera.forward() * 8.0f;
}

// Adds the fields a drag changed, already applied, to the commands of one undo step.
void addTransformEdit(std::vector<std::unique_ptr<Command>>& commands, core::Uuid entity, const scene::Transform& before,
                      const scene::Transform& after)
{
    const auto add = [&](const char* field, reflection::ValueKind kind, const void* from, const void* to) {
        commands.push_back(makeSetFieldCommand(entity, "Transform", field, scene::writeFieldValue(kind, from),
                                               scene::writeFieldValue(kind, to)));
    };
    if (before.position != after.position)
    {
        add("position", reflection::ValueKind::Vec3, &before.position, &after.position);
    }
    if (before.rotation != after.rotation)
    {
        add("rotation", reflection::ValueKind::Quat, &before.rotation, &after.rotation);
    }
    if (before.scale != after.scale)
    {
        add("scale", reflection::ValueKind::Vec3, &before.scale, &after.scale);
    }
}

// Records what the gizmo did to the active entity and to the others, as one undo step.
void finishGizmoDrag(ToolsState& state, scene::Scene& scene, core::Uuid active)
{
    std::vector<std::unique_ptr<Command>> commands;
    if (const scene::Transform* const local = scene.tryGet<scene::Transform>(scene.findEntity(active)))
    {
        addTransformEdit(commands, active, state.gizmo.startTransform(), *local);
    }
    for (const GizmoFollower& follower : state.gizmoFollowers)
    {
        if (const scene::Transform* const local = scene.tryGet<scene::Transform>(scene.findEntity(follower.entity)))
        {
            addTransformEdit(commands, follower.entity, follower.local, *local);
        }
    }
    const std::size_t moved = state.gizmoFollowers.size() + 1;
    if (commands.size() == 1)
    {
        state.history.recordApplied(std::move(commands.front()));
    }
    else if (!commands.empty())
    {
        state.history.recordApplied(makeCompositeCommand(std::move(commands), std::format("Transform {} entities", moved)));
    }
    state.gizmoFollowers.clear();
    state.gizmo.end();
}

// Moves, turns or scales the other selected entities as the gizmo does the active one, each about
// its own origin, as Unity's Pivot mode does.
void moveFollowers(ToolsState& state, scene::Scene& scene, const math::Mat4& activeParentWorld,
                   const scene::Transform& activeLocal)
{
    const scene::Transform& start = state.gizmo.startTransform();
    const math::Mat4 startWorld = activeParentWorld * start.matrix();
    const math::Mat4 newWorld = activeParentWorld * activeLocal.matrix();
    const math::Trs startTrs = math::decomposeTrs(startWorld);
    const math::Trs newTrs = math::decomposeTrs(newWorld);
    for (const GizmoFollower& follower : state.gizmoFollowers)
    {
        scene::Transform* const local = scene.tryGet<scene::Transform>(scene.findEntity(follower.entity));
        if (local == nullptr)
        {
            continue;
        }
        scene::Transform moved = follower.local;
        switch (state.gizmo.mode)
        {
        case GizmoMode::Translate: {
            const math::Vec3 position = math::Vec3(follower.world[3]) + (newTrs.translation - startTrs.translation);
            moved.position = math::Vec3(math::inverse(follower.parentWorld) * math::Vec4(position, 1.0f));
            break;
        }
        case GizmoMode::Rotate: {
            const math::Quat turn = newTrs.rotation * math::conjugate(startTrs.rotation);
            const math::Quat parentRotation = math::decomposeTrs(follower.parentWorld).rotation;
            const math::Quat world = turn * math::decomposeTrs(follower.world).rotation;
            moved.rotation = math::normalize(math::conjugate(parentRotation) * world);
            break;
        }
        case GizmoMode::Scale: {
            const auto ratio = [](float to, float from) { return std::abs(from) > 1e-6f ? to / from : 1.0f; };
            moved.scale *= math::Vec3{ratio(activeLocal.scale.x, start.scale.x), ratio(activeLocal.scale.y, start.scale.y),
                                      ratio(activeLocal.scale.z, start.scale.z)};
            break;
        }
        }
        *local = moved;
    }
}

void handleCamera(ToolsState& state, bool hovered, math::Vec2 size, math::Vec2 mouse)
{
    const platform::Input& input = state.platform.input();

    // In 2D, the right and the middle buttons slide the view, and the wheel zooms at the mouse.
    if (state.camera.isTwoD())
    {
        if (hovered && (state.input.clicked(Mouse::Right) || state.input.clicked(Mouse::Middle)))
        {
            state.panning = true;
            state.hosts.focusCurrent();
        }
        if (state.panning)
        {
            state.panning = state.input.down(Mouse::Right) || state.input.down(Mouse::Middle);
            state.camera.pan(math::Vec2(state.input.mouseDelta().x, state.input.mouseDelta().y) * state.pixelsPerPoint, size.y);
        }
        if (hovered && state.input.wheel().y != 0.0f)
        {
            state.camera.zoomAt(state.input.wheel().y, mouse, size);
        }
        return;
    }

    if (hovered && state.input.clicked(Mouse::Right))
    {
        state.flying = true;
        state.window.setMouseCaptured(true);
        state.hosts.focusCurrent();
    }
    if (state.flying)
    {
        if (!state.input.down(Mouse::Right) && !input.isMouseButtonDown(MouseButton::Right))
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
            state.camera.fly(math::Vec3{axis(Key::D, Key::A), axis(Key::E, Key::Q), axis(Key::S, Key::W)}, state.input.delta(),
                             input.isKeyDown(Key::LeftShift) || input.isKeyDown(Key::RightShift));
            if (state.input.wheel().y != 0.0f)
            {
                state.camera.changeSpeed(state.input.wheel().y);
            }
        }
    }

    if (hovered && state.input.clicked(Mouse::Middle))
    {
        state.panning = true;
    }
    if (state.panning)
    {
        state.panning = state.input.down(Mouse::Middle);
        state.camera.pan(math::Vec2(state.input.mouseDelta().x, state.input.mouseDelta().y) * state.pixelsPerPoint, size.y);
    }

    if (hovered && state.input.alt() && state.input.clicked(Mouse::Left))
    {
        state.orbiting = true;
    }
    if (state.orbiting)
    {
        state.orbiting = state.input.down(Mouse::Left);
        state.camera.orbit(math::Vec2(state.input.mouseDelta().x, state.input.mouseDelta().y));
    }

    if (hovered && !state.flying && state.input.wheel().y != 0.0f)
    {
        state.camera.dolly(state.input.wheel().y);
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
    const bool navigating = state.flying || state.orbiting || state.panning;
    const core::Uuid activeUuid = state.selection.active();
    const scene::Entity active = scene.findEntity(activeUuid);
    scene::Transform* const local = active.isValid() ? scene.tryGet<scene::Transform>(active) : nullptr;

    if (local != nullptr && scene.has<scene::WorldTransform>(active) && !navigating && state.tool != EditorTool::Select)
    {
        const math::Mat4 world = worldMatrixOf(scene, active);
        const math::Mat4 parentWorld = worldMatrixOf(scene, scene.parent(active));
        if (!state.gizmo.isDragging())
        {
            state.hoveredHandle = hovered ? state.gizmo.hitTest(view, world, mouse) : GizmoHandle::None;
            if (hovered && !state.input.alt() && state.hoveredHandle != GizmoHandle::None && state.input.clicked(Mouse::Left))
            {
                state.gizmo.begin(state.hoveredHandle, view, world, parentWorld, *local, mouse);
                // The other selected entities follow, except those that their ancestors carry. When
                // the active entity is carried by a selected ancestor, the gizmo moves the ancestors.
                state.gizmoFollowers.clear();
                for (const scene::Entity root : selectedRoots(scene, state.selection))
                {
                    if (root != active && scene.has<scene::Transform>(root) && !scene::isInsidePrefabInstance(scene, root))
                    {
                        state.gizmoFollowers.push_back({.entity = scene.uuid(root),
                                                        .local = scene.get<scene::Transform>(root),
                                                        .world = worldMatrixOf(scene, root),
                                                        .parentWorld = worldMatrixOf(scene, scene.parent(root))});
                    }
                }
            }
        }
        if (state.gizmo.isDragging())
        {
            if (state.input.down(Mouse::Left))
            {
                const scene::Transform dragged = state.gizmo.drag(view, mouse, state.input.ctrl() != state.snap);
                const bool carried = isUnderAny(scene, scene.parent(active), state.selection);
                *local = carried ? state.gizmo.startTransform() : dragged;
                moveFollowers(state, scene, parentWorld, dragged);
            }
            else
            {
                finishGizmoDrag(state, scene, activeUuid);
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
            finishGizmoDrag(state, scene, activeUuid);
        }
    }

    if (hovered && !state.input.alt() && !navigating && state.hoveredHandle == GizmoHandle::None &&
        state.input.clicked(Mouse::Left))
    {
        state.clickStart = mouse;
        state.drawingRectangle = false;
    }
    if (!state.clickStart)
    {
        return;
    }
    const float tolerance = clickTolerance * state.pixelsPerPoint;
    if (state.input.down(Mouse::Left))
    {
        // A press that moves draws a rectangle.
        state.drawingRectangle |= math::length(mouse - *state.clickStart) > tolerance;
        if (state.drawingRectangle)
        {
            const ImVec2 from(state.viewportOrigin.x + state.clickStart->x / state.pixelsPerPoint,
                              state.viewportOrigin.y + state.clickStart->y / state.pixelsPerPoint);
            const ImVec2 to = pointOf(state.input.mouse());
            state.viewportMarks.selecting =
                std::pair{ImVec2(std::min(from.x, to.x), std::min(from.y, to.y)), ImVec2(std::max(from.x, to.x), std::max(from.y, to.y))};
        }
        return;
    }

    const SelectMode mode = selectMode(state);
    if (state.drawingRectangle)
    {
        const math::Vec2 min = math::min(*state.clickStart, mouse);
        const math::Vec2 max = math::max(*state.clickStart, mouse);
        state.pickQuery = PickQuery{.purpose = PickQuery::Purpose::Rectangle,
                                    .min = min,
                                    .max = max,
                                    .mode = mode,
                                    .icons = iconsIn(state, scene, view, min, max)};
    }
    else if (const scene::Entity icon = iconAt(state, scene, view, mouse); icon.isValid())
    {
        const std::array picked{scene.uuid(clickTarget(scene, icon, state.selection.active()))};
        selectEntities(state, picked, mode);
    }
    else
    {
        state.pickQuery = PickQuery{.purpose = PickQuery::Purpose::Click, .min = mouse, .max = mouse, .mode = mode};
    }
    state.clickStart.reset();
    state.drawingRectangle = false;
}

// A material dragged over the viewport goes to the surface under the mouse, which is outlined
// until it is dropped.
void handleMaterialDrop(ToolsState& state, scene::Scene& scene, math::Vec2 mouse, bool hovered)
{
    const EditorDrag* const payload = editorUiKit(state).carried();
    AssetPayload dragged{};
    const bool material = payload != nullptr && payload->is(assetPayload, sizeof(AssetPayload)) &&
                          (std::memcpy(&dragged, payload->payload.data(), sizeof(dragged)), dragged.type == asset::AssetType::Material);
    if (!material || !hovered)
    {
        state.materialTarget = {};
        return;
    }
    // The surface under the mouse, asked again as soon as the previous answer came.
    if (state.awaitedPick == 0 && !state.pickQuery)
    {
        state.pickQuery = PickQuery{.purpose = PickQuery::Purpose::MaterialTarget, .min = mouse, .max = mouse};
    }
    const std::optional<asset::AssetId> dropped = acceptDroppedAsset(state, asset::AssetType::Material);
    const scene::Entity target = scene.findEntity(state.materialTarget);
    if (!dropped || !target.isValid())
    {
        return;
    }
    // The first field of the entity's components that holds a material.
    for (const scene::ComponentType& type : scene::componentRegistry().types())
    {
        const void* const component = type.find(scene, target);
        if (component == nullptr)
        {
            continue;
        }
        for (const reflection::FieldInfo& field : type.type->fields)
        {
            if (field.kind != reflection::ValueKind::AssetId || field.list != nullptr ||
                asset::parseAssetType(field.assetType) != asset::AssetType::Material)
            {
                continue;
            }
            const serialization::TextValue before = scene::writeFieldValue(field, field.address(component));
            const serialization::TextValue after = scene::writeFieldValue(reflection::ValueKind::AssetId, &*dropped);
            if (before != after)
            {
                state.pendingCommand =
                    makeSetFieldCommand(state.materialTarget, std::string(type.name()), field.name, before, after);
            }
            state.selection.set(state.materialTarget);
            state.materialTarget = {};
            return;
        }
    }
    DEVEX_LOG_WARNING("{} has no material to replace", scene.name(target));
    state.materialTarget = {};
}

void handleKeys(ToolsState& state, scene::Scene& scene)
{
    // Ctrl and a letter belong to the editing shortcuts: Ctrl+X cuts, it does not switch the space.
    if (state.flying || state.typing || state.input.ctrl())
    {
        return;
    }
    if (state.input.letterPressed('q', false))
    {
        state.tool = EditorTool::Select;
    }
    if (state.input.letterPressed('w', false))
    {
        state.tool = EditorTool::Move;
    }
    if (state.input.letterPressed('e', false))
    {
        state.tool = EditorTool::Rotate;
    }
    if (state.input.letterPressed('r', false))
    {
        state.tool = EditorTool::Scale;
    }
    if (state.input.letterPressed('x', false))
    {
        state.gizmo.space = state.gizmo.space == GizmoSpace::World ? GizmoSpace::Local : GizmoSpace::World;
    }
    if (state.input.letterPressed('f', false))
    {
        frameSelection(state, scene);
    }
}

// Over the screen of the other kind, which shows nothing of the scene: why, and where it is edited.
void markOtherKindHint(ToolsState& state, const scene::Scene& scene, ScreenContent content)
{
    const char* hint = nullptr;
    if (content == ScreenContent::Nothing)
    {
        hint = "A 2D scene: it is edited in the 2D screen";
    }
    else if (!state.interfaceFrame || scene.view<scene::Canvas>().begin() == scene.view<scene::Canvas>().end())
    {
        hint = "A 3D scene: its interfaces are edited here, and the scene in the 3D screen";
    }
    if (hint != nullptr)
    {
        state.viewportMarks.hint = hint;
    }
}

} // namespace

void frameSelection(ToolsState& state, const scene::Scene& scene)
{
    // The elements of an interface, where the 2D screen draws them.
    if (const std::optional<std::pair<math::Vec2, math::Vec2>> elements = selectedInterfaceBounds(state, scene))
    {
        const math::Vec2 center = (elements->first + elements->second) * 0.5f;
        state.camera.frame(math::Vec3(center, 0.0f), math::length(elements->second - elements->first) * 0.5f);
        return;
    }
    // A sphere around every selected entity, each as large as it looks.
    std::optional<std::pair<math::Vec3, math::Vec3>> bounds;
    for (const core::Uuid uuid : state.selection.entities())
    {
        const scene::Entity entity = scene.findEntity(uuid);
        if (!entity.isValid())
        {
            continue;
        }
        const math::Trs world = math::decomposeTrs(worldMatrixOf(scene, entity));
        const float largest = std::max({std::abs(world.scale.x), std::abs(world.scale.y), std::abs(world.scale.z)});
        // Built-in meshes span one unit; hierarchies such as models are usually larger.
        const float radius = scene.has<scene::MeshRenderer>(entity) ? largest * 0.87f
                             : scene.firstChild(entity).isValid() ? std::max(largest, 1.0f) * 2.0f
                                                                  : 1.0f;
        const math::Vec3 low = world.translation - math::Vec3{radius};
        const math::Vec3 high = world.translation + math::Vec3{radius};
        bounds = bounds ? std::pair{math::min(bounds->first, low), math::max(bounds->second, high)} : std::pair{low, high};
    }
    if (bounds)
    {
        state.camera.frame((bounds->first + bounds->second) * 0.5f, math::length(bounds->second - bounds->first) * 0.5f);
    }
}

void drawViewportPanel(ToolsState& state, scene::Scene& scene)
{
    DEVEX_PROFILE_SCOPE("Viewport panel");
    state.viewportMarks = ViewportMarks{};
    state.viewportHovered = false;
    state.viewportFocused = false;
    state.interfaceFrame.reset();
    if (!state.showViewport)
    {
        state.viewportPixels = {};
        return;
    }

    if (std::exchange(state.focusViewport, false))
    {
        focusPanel(state, viewportWindow);
    }
    const ThemeColors& colors = themeColors();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, uiColor(colors.outer));
    const bool open = beginDockedPanel(state, viewportWindow);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (!open)
    {
        state.viewportPixels = {};
        return;
    }

    const bool editing = state.playState == PlayState::Editing;
    // The tabs of the scenes, and the toolbar under them.
    drawViewportHeader(state, scene);

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 available = state.hosts.available();
    state.pixelsPerPoint = io.DisplayFramebufferScale.x > 0.0f ? io.DisplayFramebufferScale.x : 1.0f;
    const auto width = static_cast<std::uint32_t>(std::max(0.0f, std::floor(available.x * state.pixelsPerPoint)));
    const auto height = static_cast<std::uint32_t>(std::max(0.0f, std::floor(available.y * state.pixelsPerPoint)));
    if (width < 8 || height < 8)
    {
        state.viewportPixels = {};
        endDockedPanel(state);
        return;
    }
    state.viewportPixels = {width, height};
    const ImVec2 origin = state.hosts.cursor();
    state.viewportOrigin = {origin.x, origin.y};

    state.hosts.image(static_cast<std::uint64_t>(state.renderer.viewportTexture()), available);
    const bool hovered = state.hosts.itemHovered();
    state.viewportHovered = hovered;
    state.viewportFocused = state.hosts.focused();
    // The game is framed in the accent colour while it runs.
    state.viewportMarks.playing = !editing;

    const math::Vec2 size{static_cast<float>(width), static_cast<float>(height)};
    const math::Vec2 mouse = (math::Vec2(pointOf(state.input.mouse()).x, pointOf(state.input.mouse()).y) - state.viewportOrigin) * state.pixelsPerPoint;
    const ScreenContent content = screenContent(state.camera.isTwoD(), scene);
    const ViewportView view{.view = state.camera.view(),
                            .verticalFov = EditorCamera::verticalFov,
                            .size = size,
                            .orthographic = state.camera.isTwoD(),
                            .orthographicSize = state.camera.orthographicSize()};
    state.gizmo.twoD = state.camera.isTwoD();
    // The 2D screen draws the interfaces in the frame of what the game shows, and the 3D screen of a
    // 3D scene over the whole view, as the game will.
    if (state.camera.isTwoD())
    {
        state.interfaceFrame = interfaceFrame(gameFrame(scene, size.x / size.y), view);
    }
    else if (content == ScreenContent::Scene && state.showInterfaces)
    {
        state.interfaceFrame = InterfaceFrame{.layoutSize = size, .offset = {0.0f, 0.0f}, .scale = 1.0f};
    }

    if (editing && content != ScreenContent::Scene)
    {
        if (hovered && (state.input.clicked(Mouse::Left) || state.input.clicked(Mouse::Middle)))
        {
            state.hosts.focusCurrent();
        }
        handleCamera(state, hovered, size, mouse);
        if (content == ScreenContent::Interfaces)
        {
            static_cast<void>(handleInterfaceEditing(state, scene, hovered));
            if (state.viewportFocused || hovered)
            {
                handleKeys(state, scene);
            }
        }
        drawInterfaceOverlay(state, scene);
        markOtherKindHint(state, scene, content);
    }
    else if (editing)
    {
        if (const std::optional<asset::AssetId> model = acceptDroppedAsset(state, asset::AssetType::Model))
        {
            requestInstantiateModel(state, *model, core::Uuid{}, dropPosition(state, view, mouse));
        }
        if (const std::optional<asset::AssetId> prefab = acceptDroppedAsset(state, asset::AssetType::Scene))
        {
            requestInstantiatePrefab(state, *prefab, core::Uuid{}, dropPosition(state, view, mouse));
        }
        if (const std::optional<asset::AssetId> sprite = acceptDroppedAsset(state, asset::AssetType::Sprite))
        {
            requestCreateSprite(state, *sprite, dropPosition(state, view, mouse));
        }
        if (hovered && (state.input.clicked(Mouse::Left) || state.input.clicked(Mouse::Middle)))
        {
            state.hosts.focusCurrent();
        }
        // A drag from another panel holds the mouse: the view counts as hovered all the same.
        handleMaterialDrop(state, scene, mouse, state.hosts.itemHovered());
        handleCamera(state, hovered, size, mouse);
        // A tile tool takes the left button from the selection and the gizmo, and the elements of
        // the interfaces take it from the world under them.
        if (!handleTilePainting(state, scene, view, mouse, hovered) && !handleInterfaceEditing(state, scene, hovered))
        {
            handleGizmoAndSelection(state, scene, view, mouse, hovered);
        }
        drawInterfaceOverlay(state, scene);
        if (state.viewportFocused || hovered)
        {
            handleKeys(state, scene);
        }
    }
    else if (hovered && state.input.clicked(Mouse::Left))
    {
        // Clicking the game gives it the keyboard.
        state.hosts.focusCurrent();
    }
    // Over everything that answered the mouse over the view.
    drawViewportOverlay(state);
    endDockedPanel(state);
}

} // namespace devex::tools::detail
