// Painting a GridMap in the 3D view, as Godot's: a piece of its palette laid on a floor of cells, turned as it goes.
#include "InspectorUi.hpp"
#include "ToolsState.hpp"

#include <devex/scene/Components.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/scene/GridMapComponents.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace devex::tools::detail {

namespace {

constexpr math::Vec4 cursorColor{1.0f, 0.75f, 0.2f, 1.0f};
constexpr math::Vec4 eraseColor{1.0f, 0.35f, 0.3f, 1.0f};
constexpr math::Vec4 floorColor{1.0f, 0.75f, 0.2f, 0.25f};
constexpr std::array<std::string_view, 1> oneNumber{""};
// How many cells of the floor are drawn around the one under the mouse.
constexpr int floorReach = 6;

[[nodiscard]] scene::GridMap* editedGrid(const ToolsState& state, scene::Scene& scene, scene::Entity& entity)
{
    entity = scene.findEntity(state.selection.active());
    return entity.isValid() ? scene.tryGet<scene::GridMap>(entity) : nullptr;
}

[[nodiscard]] math::Mat4 worldOf(const scene::Scene& scene, scene::Entity entity)
{
    const scene::WorldTransform* const world = scene.tryGet<scene::WorldTransform>(entity);
    return world != nullptr ? world->matrix : math::Mat4{1.0f};
}

// The cell under the mouse on the floor it paints on.
[[nodiscard]] std::optional<math::IVec3> cellUnder(const scene::GridMap& grid, const math::Mat4& world, const ViewportView& view, math::Vec2 mouse, std::int32_t floor)
{
    const Ray ray = view.ray(mouse);
    const math::Mat4 toLocal = math::inverse(world);
    const math::Vec3 origin = math::Vec3(toLocal * math::Vec4(ray.origin, 1.0f));
    const math::Vec3 direction = math::Vec3(toLocal * math::Vec4(ray.direction, 0.0f));
    const float height = static_cast<float>(floor) * grid.cellSize.y;
    if (std::abs(direction.y) < 1.0e-5f)
    {
        return std::nullopt;
    }
    const float distance = (height - origin.y) / direction.y;
    if (distance <= 0.0f)
    {
        return std::nullopt;
    }
    math::IVec3 cell = scene::gridCellAt(grid, origin + direction * distance);
    cell.y = floor;
    return cell;
}

void finishStroke(ToolsState& state, const scene::Scene& scene)
{
    GridEditor& editor = state.gridEditor;
    if (!editor.stroking)
    {
        return;
    }
    editor.stroking = false;
    const scene::Entity entity = scene.findEntity(editor.target);
    const scene::GridMap* const grid = entity.isValid() ? scene.tryGet<scene::GridMap>(entity) : nullptr;
    const scene::ComponentType* const type = scene::componentRegistry().find("GridMap");
    if (grid == nullptr || type == nullptr)
    {
        return;
    }
    const reflection::FieldInfo* const field = type->type->findField("cells");
    serialization::TextValue after = scene::writeFieldValue(*field, field->address(grid));
    if (after != editor.before)
    {
        state.history.recordApplied(makeSetFieldCommand(editor.target, "GridMap", "cells", editor.before, std::move(after)));
    }
}

} // namespace

struct GridEditorUi
{
    PanelButton paint;
    PanelButton erase;
    PanelButton rotate;
    scene::Entity piece;
    scene::Entity floor;
    scene::Entity hint;
};

void addGridMapEditor(InspectorUi& ui, EditorUiKit& kit, Section& section)
{
    auto editor = std::make_shared<GridEditorUi>();
    const int group = ui.addGroup(section, "Draw in the View");
    const auto firstLine = section.lines.size();
    const scene::Entity row = ui.actions(&section);
    editor->paint = ui.action(kit, row, std::nullopt, "Paint");
    ui.tooltip(editor->paint.entity, "Click and drag in the view to lay the piece on the floor. Shift takes pieces away");
    editor->erase = ui.action(kit, row, std::nullopt, "Erase");
    ui.tooltip(editor->erase.entity, "Click and drag in the view to take pieces away");
    editor->rotate = ui.action(kit, row, std::nullopt, "Rotate");
    ui.tooltip(editor->rotate.entity, "Turn the piece by a quarter (Space)");
    const FormRow pieceRow = ui.formRow(section, "Piece");
    editor->piece = ui.choice(pieceRow.editor);
    ui.tooltip(pieceRow.row, "The piece to lay: a top entity of the palette");
    const FormRow floorRow = ui.formRow(section, "Floor");
    editor->floor = ui.numbers(floorRow.editor, oneNumber, {.step = 1.0f, .dragSpeed = 0.1f, .decimals = 0}).front();
    ui.tooltip(floorRow.row, "The level of cells to paint on ([ and ] in the view)");
    editor->hint = ui.note(&section, "", "dim", 2.0f);
    for (std::size_t index = firstLine; index < section.lines.size(); ++index)
    {
        section.lines[index].group = group;
    }
    section.gridEditor = std::move(editor);
}

void syncGridMapEditor(InspectorUi& ui, const ToolsState& state, const scene::Scene& edited, scene::Entity entity, Section& section)
{
    GridEditorUi& shown = *section.gridEditor;
    const GridEditor& editor = state.gridEditor;
    ui.scene().get<scene::UiRect>(shown.paint.entity).style = editor.tool == GridTool::Paint ? "primary" : "button";
    ui.scene().get<scene::UiRect>(shown.erase.entity).style = editor.tool == GridTool::Erase ? "primary" : "button";
    const scene::GridMap* const grid = edited.tryGet<scene::GridMap>(entity);
    if (grid == nullptr)
    {
        return;
    }
    const std::shared_ptr<const scene::GridPalette> palette = scene::gridPalette(grid->palette);
    std::vector<std::string> names;
    if (palette != nullptr)
    {
        for (const scene::GridPiece& piece : palette->pieces)
        {
            names.push_back(piece.name.empty() ? std::format("Piece {}", names.size()) : piece.name);
        }
    }
    ui.setChoice(shown.piece, std::move(names), editor.piece);
    if (ui.panel.world().editedField() != shown.floor)
    {
        ui.setNumber(shown.floor, static_cast<float>(editor.floor));
    }
    ui.scene().get<scene::UiText>(shown.hint).text = palette != nullptr
                                                         ? std::format("{} cells. Quarter turns: {}. Escape stops drawing.", scene::gridCellCount(*grid), editor.rotation)
                                                         : std::string("Choose a palette: a scene whose top entities are the pieces.");
}

void answerGridMapEditor(InspectorUi& ui, ToolsState& state, Section& section)
{
    const GridEditorUi& shown = *section.gridEditor;
    GridEditor& editor = state.gridEditor;
    const ui::UiWorld& world = ui.panel.world();
    if (world.wasClicked(shown.paint.entity))
    {
        editor.tool = editor.tool == GridTool::Paint ? GridTool::None : GridTool::Paint;
    }
    if (world.wasClicked(shown.erase.entity))
    {
        editor.tool = editor.tool == GridTool::Erase ? GridTool::None : GridTool::Erase;
    }
    if (world.wasClicked(shown.rotate.entity))
    {
        editor.rotation = (editor.rotation + 1) % 4;
    }
    if (world.wasChanged(shown.piece))
    {
        editor.piece = ui.scene().get<scene::UiDropdown>(shown.piece).selected;
    }
    if (world.wasChanged(shown.floor))
    {
        editor.floor = static_cast<std::int32_t>(std::round(ui.scene().get<scene::UiNumberField>(shown.floor).value));
    }
}

bool handleGridPainting(ToolsState& state, scene::Scene& scene, const ViewportView& view, math::Vec2 mouse, bool hovered)
{
    GridEditor& editor = state.gridEditor;
    scene::Entity entity;
    scene::GridMap* const grid = editedGrid(state, scene, entity);
    if (editor.tool == GridTool::None || grid == nullptr || state.playState != PlayState::Editing)
    {
        finishStroke(state, scene);
        editor.hovered.reset();
        return false;
    }
    if (state.viewportFocused && !editor.stroking)
    {
        if (state.input.pressed(platform::Key::Escape, true))
        {
            editor.tool = GridTool::None;
            editor.hovered.reset();
            return false;
        }
        if (state.input.pressed(platform::Key::LeftBracket, true))
        {
            --editor.floor;
        }
        if (state.input.pressed(platform::Key::RightBracket, true))
        {
            ++editor.floor;
        }
        if (state.input.pressed(platform::Key::Space, true))
        {
            editor.rotation = (editor.rotation + 1) % 4;
        }
    }
    const std::optional<math::IVec3> cell = cellUnder(*grid, worldOf(scene, entity), view, mouse, editor.floor);
    editor.hovered = hovered || editor.stroking ? cell : std::nullopt;

    if (hovered && !editor.stroking && state.input.clicked(Mouse::Left))
    {
        state.viewportFocused = true;
        const scene::ComponentType* const type = scene::componentRegistry().find("GridMap");
        const reflection::FieldInfo* const field = type->type->findField("cells");
        editor.before = scene::writeFieldValue(*field, field->address(grid));
        editor.target = scene.uuid(entity);
        editor.stroking = true;
        editor.lastCell.reset();
    }
    if (editor.stroking && cell)
    {
        // Every cell on the way from the last one: the mouse goes further than a cell in a frame.
        const math::IVec3 from = editor.lastCell.value_or(*cell);
        const int steps = std::max(std::abs(cell->x - from.x), std::abs(cell->z - from.z));
        for (int step = 0; step <= steps; ++step)
        {
            const float amount = steps != 0 ? static_cast<float>(step) / static_cast<float>(steps) : 1.0f;
            const math::IVec3 passed{static_cast<int>(std::round(std::lerp(static_cast<float>(from.x), static_cast<float>(cell->x), amount))), cell->y,
                                     static_cast<int>(std::round(std::lerp(static_cast<float>(from.z), static_cast<float>(cell->z), amount)))};
            if (editor.tool == GridTool::Erase || state.input.shift())
            {
                scene::removeGridCell(*grid, passed);
            }
            else
            {
                scene::setGridCell(*grid, passed, editor.piece, editor.rotation);
            }
        }
        editor.lastCell = *cell;
    }
    if (editor.stroking && state.input.released(Mouse::Left))
    {
        finishStroke(state, scene);
    }
    return true;
}

void addGridMapOverlay(ToolsState& state, scene::Scene& scene, std::vector<render::OverlayVertex>& lines)
{
    const GridEditor& editor = state.gridEditor;
    scene::Entity entity;
    const scene::GridMap* const grid = editedGrid(state, scene, entity);
    if (editor.tool == GridTool::None || grid == nullptr || !editor.hovered)
    {
        return;
    }
    const math::Mat4 world = worldOf(scene, entity);
    const auto line = [&](math::Vec3 from, math::Vec3 to, math::Vec4 color) {
        lines.push_back({math::Vec3(world * math::Vec4(from, 1.0f)), color});
        lines.push_back({math::Vec3(world * math::Vec4(to, 1.0f)), color});
    };
    const math::Vec3 size = grid->cellSize;
    const math::IVec3 cell = *editor.hovered;
    const float y = static_cast<float>(cell.y) * size.y;

    // The floor it paints on, around the cell.
    for (int step = -floorReach; step <= floorReach + 1; ++step)
    {
        const float x = static_cast<float>(cell.x + step) * size.x;
        const float z = static_cast<float>(cell.z + step) * size.z;
        line({x, y, static_cast<float>(cell.z - floorReach) * size.z}, {x, y, static_cast<float>(cell.z + floorReach + 1) * size.z}, floorColor);
        line({static_cast<float>(cell.x - floorReach) * size.x, y, z}, {static_cast<float>(cell.x + floorReach + 1) * size.x, y, z}, floorColor);
    }

    // The cell itself, and which way the piece looks.
    const bool erasing = editor.tool == GridTool::Erase || state.input.shift();
    const math::Vec4 color = erasing ? eraseColor : cursorColor;
    const math::Vec3 low{static_cast<float>(cell.x) * size.x, y, static_cast<float>(cell.z) * size.z};
    const math::Vec3 high = low + size;
    for (const float height : {low.y, high.y})
    {
        line({low.x, height, low.z}, {high.x, height, low.z}, color);
        line({high.x, height, low.z}, {high.x, height, high.z}, color);
        line({high.x, height, high.z}, {low.x, height, high.z}, color);
        line({low.x, height, high.z}, {low.x, height, low.z}, color);
    }
    for (const math::Vec2 corner : {math::Vec2{low.x, low.z}, math::Vec2{high.x, low.z}, math::Vec2{high.x, high.z}, math::Vec2{low.x, high.z}})
    {
        line({corner.x, low.y, corner.y}, {corner.x, high.y, corner.y}, color);
    }
    if (!erasing)
    {
        const scene::GridCell placed{.position = cell, .piece = editor.piece, .rotation = editor.rotation};
        const math::Mat4 place = scene::gridCellMatrix(*grid, placed);
        const math::Vec3 middle = math::Vec3(place * math::Vec4(0.0f, 0.02f, 0.0f, 1.0f));
        const math::Vec3 front = math::Vec3(place * math::Vec4(0.0f, 0.02f, -size.z * 0.5f, 1.0f));
        line(middle, front, color);
    }
}

} // namespace devex::tools::detail
