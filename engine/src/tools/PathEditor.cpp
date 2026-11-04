// Drawing a Path3D in the 3D view, as Godot's Path3D tools do: adding points at its end and pulling their
// handles out, moving what is there, and taking points away.
#include "InspectorUi.hpp"
#include "ToolsState.hpp"

#include <devex/scene/Components.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/scene/PathComponents.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace devex::tools::detail {

namespace {

// How close to a point or a handle the mouse must be to take it, in pixels.
constexpr float pickDistance = 10.0f;
constexpr math::Vec4 curveColor{0.3f, 0.85f, 0.75f, 1.0f};
constexpr math::Vec4 otherCurveColor{0.3f, 0.85f, 0.75f, 0.35f};
constexpr math::Vec4 pointColor{1.0f, 0.75f, 0.2f, 1.0f};
constexpr math::Vec4 selectedColor{1.0f, 1.0f, 1.0f, 1.0f};
constexpr math::Vec4 handleColor{0.55f, 0.7f, 1.0f, 1.0f};

struct ToolChoice
{
    PathTool tool;
    std::string_view label;
    std::string_view tooltip;
};
constexpr std::array<ToolChoice, 3> toolChoices{{
    {PathTool::Move, "Move", "Drag a point or a handle in the view, on the level it is at. Ctrl drags it up and down, and Shift pulls a handle on its own"},
    {PathTool::Add, "Add", "Click in the view to add a point at the end of the curve, and drag to pull its handles. Clicking on the first point closes the curve"},
    {PathTool::Remove, "Remove", "Click on a point in the view to take it away"},
}};

[[nodiscard]] scene::Path3D* editedPath(const ToolsState& state, scene::Scene& scene, scene::Entity& entity)
{
    entity = scene.findEntity(state.selection.active());
    return entity.isValid() ? scene.tryGet<scene::Path3D>(entity) : nullptr;
}

[[nodiscard]] math::Mat4 worldOf(const scene::Scene& scene, scene::Entity entity)
{
    const scene::WorldTransform* const world = scene.tryGet<scene::WorldTransform>(entity);
    return world != nullptr ? world->matrix : math::Mat4{1.0f};
}

[[nodiscard]] math::Vec3 place(const math::Mat4& world, math::Vec3 local)
{
    return math::Vec3(world * math::Vec4(local, 1.0f));
}

// The three lists stay as long as the points, which also repairs a file written by hand.
void matchHandles(scene::Path3D& path)
{
    path.handlesIn.resize(path.points.size(), math::Vec3{0.0f});
    path.handlesOut.resize(path.points.size(), math::Vec3{0.0f});
}

struct Picked
{
    std::size_t point = 0;
    PathPart part = PathPart::Point;
};

// What is under the mouse: a handle of the point in hand before the points, which may be under it.
[[nodiscard]] std::optional<Picked> pick(const scene::Path3D& path, const math::Mat4& world, const ViewportView& view, math::Vec2 mouse, std::optional<std::size_t> selected,
                                         bool handles)
{
    float best = pickDistance;
    std::optional<Picked> found;
    const auto consider = [&](math::Vec3 local, Picked candidate) {
        if (const std::optional<math::Vec2> pixel = view.project(place(world, local)))
        {
            if (const float away = math::length(*pixel - mouse); away < best)
            {
                best = away;
                found = candidate;
            }
        }
    };
    for (std::size_t index = 0; index < path.points.size(); ++index)
    {
        consider(path.points[index], {index, PathPart::Point});
    }
    if (handles && selected && *selected < path.points.size())
    {
        const std::size_t index = *selected;
        const float pointBest = best;
        best = pickDistance;
        std::optional<Picked> handle;
        std::swap(found, handle);
        if (index < path.handlesIn.size())
        {
            consider(path.points[index] + path.handlesIn[index], {index, PathPart::HandleIn});
        }
        if (index < path.handlesOut.size())
        {
            consider(path.points[index] + path.handlesOut[index], {index, PathPart::HandleOut});
        }
        if (!found)
        {
            found = handle;
            best = pointBest;
        }
    }
    return found;
}

// Where the mouse points on the level of a position of the world; with `vertical`, above or below it.
[[nodiscard]] std::optional<math::Vec3> pointed(const ViewportView& view, math::Vec2 mouse, math::Vec3 position, bool vertical)
{
    const Ray ray = view.ray(mouse);
    if (vertical)
    {
        const std::optional<float> along = closestParameterOnLine(position, math::Vec3{0.0f, 1.0f, 0.0f}, ray);
        return along ? std::optional(position + math::Vec3{0.0f, *along, 0.0f}) : std::nullopt;
    }
    if (std::abs(ray.direction.y) < 1.0e-4f)
    {
        return std::nullopt;
    }
    const float distance = (position.y - ray.origin.y) / ray.direction.y;
    return distance > 0.0f ? std::optional(ray.origin + ray.direction * distance) : std::nullopt;
}

// One step of the history: the three lists, from what they were to what they are.
void commit(ToolsState& state, const scene::Scene& scene, PathEditor& editor)
{
    const scene::Entity entity = scene.findEntity(editor.target);
    const scene::Path3D* const path = entity.isValid() ? scene.tryGet<scene::Path3D>(entity) : nullptr;
    const scene::ComponentType* const type = scene::componentRegistry().find("Path3D");
    if (path == nullptr || type == nullptr)
    {
        return;
    }
    std::vector<std::unique_ptr<Command>> commands;
    const std::array<std::pair<const char*, const serialization::TextValue*>, 4> fields{{
        {"points", &editor.points},
        {"handles_in", &editor.handlesIn},
        {"handles_out", &editor.handlesOut},
        {"closed", &editor.closed},
    }};
    for (const auto& [name, before] : fields)
    {
        const reflection::FieldInfo* const field = type->type->findField(name);
        serialization::TextValue after = scene::writeFieldValue(*field, field->address(path));
        if (after != *before)
        {
            commands.push_back(makeSetFieldCommand(editor.target, "Path3D", name, *before, std::move(after)));
        }
    }
    if (!commands.empty())
    {
        state.history.recordApplied(makeCompositeCommand(std::move(commands), "Edit Path"));
    }
}

void remember(PathEditor& editor, const scene::Scene& scene, scene::Entity entity, const scene::Path3D& path)
{
    const scene::ComponentType* const type = scene::componentRegistry().find("Path3D");
    const auto value = [&](const char* name) {
        const reflection::FieldInfo* const field = type->type->findField(name);
        return scene::writeFieldValue(*field, field->address(&path));
    };
    editor.target = scene.uuid(entity);
    editor.points = value("points");
    editor.handlesIn = value("handles_in");
    editor.handlesOut = value("handles_out");
    editor.closed = value("closed");
}

} // namespace

struct PathEditorUi
{
    std::array<PanelButton, toolChoices.size()> tools;
    scene::Entity hint;
};

void addPathEditor(InspectorUi& ui, EditorUiKit& kit, Section& section)
{
    auto editor = std::make_shared<PathEditorUi>();
    const int group = ui.addGroup(section, "Draw in the View");
    const auto firstLine = section.lines.size();
    const scene::Entity row = ui.actions(&section);
    for (std::size_t index = 0; index < toolChoices.size(); ++index)
    {
        editor->tools[index] = ui.action(kit, row, std::nullopt, toolChoices[index].label);
        ui.tooltip(editor->tools[index].entity, std::string(toolChoices[index].tooltip));
    }
    editor->hint = ui.note(&section, "", "dim", 2.0f);
    for (std::size_t index = firstLine; index < section.lines.size(); ++index)
    {
        section.lines[index].group = group;
    }
    section.pathEditor = std::move(editor);
}

void syncPathEditor(InspectorUi& ui, const ToolsState& state, const scene::Scene& edited, scene::Entity entity, Section& section)
{
    PathEditorUi& shown = *section.pathEditor;
    for (std::size_t index = 0; index < toolChoices.size(); ++index)
    {
        ui.scene().get<scene::UiRect>(shown.tools[index].entity).style = state.pathEditor.tool == toolChoices[index].tool ? "primary" : "button";
    }
    const scene::Path3D* const path = edited.tryGet<scene::Path3D>(entity);
    if (path != nullptr)
    {
        const scene::PathCurve curve(*path);
        ui.scene().get<scene::UiText>(shown.hint).text = std::format("{} points, {:.1f} m. Escape stops drawing.", path->points.size(), curve.length());
    }
}

void answerPathEditor(InspectorUi& ui, ToolsState& state, Section& section)
{
    const PathEditorUi& shown = *section.pathEditor;
    const ui::UiWorld& world = ui.panel.world();
    for (std::size_t index = 0; index < toolChoices.size(); ++index)
    {
        if (world.wasClicked(shown.tools[index].entity))
        {
            state.pathEditor.tool = state.pathEditor.tool == toolChoices[index].tool ? PathTool::None : toolChoices[index].tool;
        }
    }
}

bool handlePathEditing(ToolsState& state, scene::Scene& scene, const ViewportView& view, math::Vec2 mouse, bool hovered)
{
    PathEditor& editor = state.pathEditor;
    scene::Entity entity;
    scene::Path3D* const path = editedPath(state, scene, entity);
    if (editor.tool == PathTool::None || path == nullptr || state.playState != PlayState::Editing)
    {
        if (editor.dragging)
        {
            editor.dragging = false;
            commit(state, scene, editor);
        }
        editor.selected.reset();
        return false;
    }
    if (state.viewportFocused && !editor.dragging && state.input.pressed(platform::Key::Escape, true))
    {
        editor.tool = PathTool::None;
        return false;
    }
    matchHandles(*path);
    const math::Mat4 world = worldOf(scene, entity);
    const math::Mat4 toLocal = math::inverse(world);

    if (editor.dragging)
    {
        if (editor.selected && *editor.selected < path->points.size())
        {
            const std::size_t index = *editor.selected;
            math::Vec3 local = path->points[index];
            if (editor.part == PathPart::HandleIn)
            {
                local += path->handlesIn[index];
            }
            else if (editor.part == PathPart::HandleOut)
            {
                local += path->handlesOut[index];
            }
            if (const std::optional<math::Vec3> target = pointed(view, mouse, place(world, local), state.input.ctrl()))
            {
                const math::Vec3 moved = place(toLocal, *target);
                if (editor.part == PathPart::Point)
                {
                    path->points[index] = moved;
                }
                else
                {
                    // Aligned on both sides, so that the curve stays smooth, unless told otherwise.
                    const math::Vec3 handle = moved - path->points[index];
                    (editor.part == PathPart::HandleIn ? path->handlesIn : path->handlesOut)[index] = handle;
                    if (!state.input.shift())
                    {
                        (editor.part == PathPart::HandleIn ? path->handlesOut : path->handlesIn)[index] = -handle;
                    }
                }
            }
        }
        if (state.input.released(Mouse::Left))
        {
            editor.dragging = false;
            commit(state, scene, editor);
        }
        return true;
    }

    if (!hovered || !state.input.clicked(Mouse::Left))
    {
        return true;
    }
    state.viewportFocused = true;
    const std::optional<Picked> picked = pick(*path, world, view, mouse, editor.selected, editor.tool == PathTool::Move);
    remember(editor, scene, entity, *path);
    switch (editor.tool)
    {
    case PathTool::Move:
        editor.selected = picked ? std::optional(picked->point) : std::nullopt;
        editor.part = picked ? picked->part : PathPart::Point;
        editor.dragging = picked.has_value();
        break;
    case PathTool::Add: {
        // On the first point, once there is a curve to close.
        if (picked && picked->point == 0 && path->points.size() >= 3)
        {
            path->closed = true;
            editor.tool = PathTool::Move;
            commit(state, scene, editor);
            break;
        }
        const math::Vec3 level = path->points.empty() ? math::Vec3(world[3]) : place(world, path->points.back());
        if (const std::optional<math::Vec3> target = pointed(view, mouse, level, false))
        {
            path->points.push_back(place(toLocal, *target));
            matchHandles(*path);
            editor.selected = path->points.size() - 1;
            editor.part = PathPart::HandleOut;
            editor.dragging = true;
        }
        break;
    }
    case PathTool::Remove:
        if (picked)
        {
            const auto index = static_cast<std::ptrdiff_t>(picked->point);
            path->points.erase(path->points.begin() + index);
            path->handlesIn.erase(path->handlesIn.begin() + index);
            path->handlesOut.erase(path->handlesOut.begin() + index);
            path->closed = path->closed && path->points.size() >= 3;
            editor.selected.reset();
            commit(state, scene, editor);
        }
        break;
    case PathTool::None:
        break;
    }
    return true;
}

void addPathOverlay(const ToolsState& state, scene::Scene& scene, const std::unordered_set<std::uint32_t>& shown, std::vector<render::OverlayVertex>& lines)
{
    const auto line = [&lines](math::Vec3 from, math::Vec3 to, math::Vec4 color) {
        lines.push_back({from, color});
        lines.push_back({to, color});
    };
    // A mark that keeps about the same size on the screen, whatever the distance of the camera.
    const math::Vec3 eye = state.camera.position();
    const auto mark = [&](math::Vec3 at, math::Vec4 color, float scale) {
        const float size = std::max(math::length(at - eye), 0.1f) * 0.009f * scale;
        line(at - math::Vec3{size, 0.0f, 0.0f}, at + math::Vec3{size, 0.0f, 0.0f}, color);
        line(at - math::Vec3{0.0f, size, 0.0f}, at + math::Vec3{0.0f, size, 0.0f}, color);
        line(at - math::Vec3{0.0f, 0.0f, size}, at + math::Vec3{0.0f, 0.0f, size}, color);
    };
    for ([[maybe_unused]] auto [entity, path] : scene.view<scene::Path3D>())
    {
        const bool selected = shown.contains(entity.index);
        const math::Mat4 world = worldOf(scene, entity);
        const scene::PathCurve curve(path);
        const std::span<const math::Vec3> points = curve.points();
        for (std::size_t index = 1; index < points.size(); ++index)
        {
            line(place(world, points[index - 1]), place(world, points[index]), selected ? curveColor : otherCurveColor);
        }
        if (!selected)
        {
            continue;
        }
        const bool editing = state.pathEditor.tool != PathTool::None && scene.uuid(entity) == state.selection.active();
        for (std::size_t index = 0; index < path.points.size(); ++index)
        {
            const math::Vec3 at = place(world, path.points[index]);
            const bool chosen = editing && state.pathEditor.selected == index;
            mark(at, chosen ? selectedColor : pointColor, 1.0f);
            if (!editing || chosen)
            {
                for (const std::vector<math::Vec3>* const handles : {&path.handlesIn, &path.handlesOut})
                {
                    if (index < handles->size() && math::length((*handles)[index]) > 1.0e-4f)
                    {
                        const math::Vec3 tip = place(world, path.points[index] + (*handles)[index]);
                        line(at, tip, handleColor);
                        mark(tip, handleColor, 0.6f);
                    }
                }
            }
        }
    }
}

} // namespace devex::tools::detail
