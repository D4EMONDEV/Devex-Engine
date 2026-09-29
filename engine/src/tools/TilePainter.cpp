#include "InspectorUi.hpp"

#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/scene/TilemapComponents.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <format>
#include <set>

namespace devex::tools::detail {
namespace {

// Flood fills stop there, and fills of the empty cells stay within the cells already painted.
constexpr std::size_t maxFilledCells = 65536;

struct ToolChoice
{
    TileTool tool;
    const char* label;
    const char* tooltip;
};

constexpr std::array toolChoices{
    ToolChoice{TileTool::Paint, "Paint", "Paint cells under the mouse, dragging (Shift erases, Ctrl picks)"},
    ToolChoice{TileTool::Erase, "Erase", "Empty cells under the mouse, dragging"},
    ToolChoice{TileTool::Rectangle, "Rectangle", "Fill the rectangle dragged (Shift empties it)"},
    ToolChoice{TileTool::Fill, "Fill", "Fill the cells like the one clicked that touch it"},
    ToolChoice{TileTool::Pick, "Pick", "Take the tile of the cell clicked, then paint with it"},
};

[[nodiscard]] const reflection::FieldInfo* blocksField()
{
    const scene::ComponentType* const type = scene::componentRegistry().find("Tilemap");
    return type != nullptr ? type->type->findField("blocks") : nullptr;
}

// The tilemap of the active entity, which the tools paint.
[[nodiscard]] scene::Tilemap* paintedTilemap(const ToolsState& state, scene::Scene& scene, scene::Entity& entity)
{
    entity = scene.findEntity(state.selection.active());
    return entity.isValid() ? scene.tryGet<scene::Tilemap>(entity) : nullptr;
}

[[nodiscard]] math::Mat4 worldOf(const scene::Scene& scene, scene::Entity entity)
{
    const scene::WorldTransform* const world = scene.tryGet<scene::WorldTransform>(entity);
    return world != nullptr ? world->matrix : math::Mat4{1.0f};
}

// The cell under a pixel: where its ray meets the plane of the tilemap.
[[nodiscard]] std::optional<math::IVec2> cellUnder(const scene::Tilemap& tilemap, const math::Mat4& world,
                                                   const ViewportView& view, math::Vec2 pixel)
{
    const Ray ray = view.ray(pixel);
    const math::Vec3 normal = math::normalize(math::Mat3(world) * math::Vec3{0.0f, 0.0f, 1.0f});
    const std::optional<math::Vec3> point = intersectPlane(ray, math::Vec3(world[3]), normal);
    if (!point)
    {
        return std::nullopt;
    }
    return scene::cellAt(tilemap, world, *point);
}

[[nodiscard]] std::uint16_t brushValue(const TilePainter& painter) noexcept
{
    if (painter.tile == 0)
    {
        return 0;
    }
    return static_cast<std::uint16_t>((painter.tile & scene::tileIdMask) | (painter.flipX ? scene::tileFlipX : 0) |
                                      (painter.flipY ? scene::tileFlipY : 0));
}

// The cells from one to the other, both included, without gaps.
template <typename Visit>
void visitLine(math::IVec2 from, math::IVec2 to, Visit&& visit)
{
    const math::IVec2 delta = math::abs(to - from);
    const math::IVec2 step{to.x > from.x ? 1 : -1, to.y > from.y ? 1 : -1};
    std::int32_t error = delta.x - delta.y;
    math::IVec2 cell = from;
    while (true)
    {
        visit(cell);
        if (cell == to)
        {
            return;
        }
        const std::int32_t twice = error * 2;
        if (twice > -delta.y)
        {
            error -= delta.y;
            cell.x += step.x;
        }
        if (twice < delta.x)
        {
            error += delta.x;
            cell.y += step.y;
        }
    }
}

[[nodiscard]] bool outside(math::IVec2 cell, const std::pair<math::IVec2, math::IVec2>& bounds) noexcept
{
    return cell.x < bounds.first.x || cell.y < bounds.first.y || cell.x > bounds.second.x || cell.y > bounds.second.y;
}

// Replaces the cells like `start` that touch it, through their sides. Empty cells are filled
// within the rectangle of the painted ones only, which bounds the fill.
void floodFill(scene::TileGrid& grid, math::IVec2 start, std::uint16_t value)
{
    const std::uint16_t replaced = grid.at(start);
    if (replaced == value)
    {
        return;
    }
    const std::optional<std::pair<math::IVec2, math::IVec2>> bounds = grid.bounds();
    if (replaced == 0 && (!bounds || outside(start, *bounds)))
    {
        grid.set(start, value);
        return;
    }
    std::deque<math::IVec2> pending{start};
    std::set<std::pair<std::int32_t, std::int32_t>> seen{{start.x, start.y}};
    std::size_t filled = 0;
    while (!pending.empty() && filled < maxFilledCells)
    {
        const math::IVec2 cell = pending.front();
        pending.pop_front();
        grid.set(cell, value);
        ++filled;
        for (const math::IVec2 offset : {math::IVec2{1, 0}, math::IVec2{-1, 0}, math::IVec2{0, 1}, math::IVec2{0, -1}})
        {
            const math::IVec2 next = cell + offset;
            if (replaced == 0 && bounds && outside(next, *bounds))
            {
                continue;
            }
            if (grid.at(next) == replaced && seen.insert({next.x, next.y}).second)
            {
                pending.push_back(next);
            }
        }
    }
}

// Records the change of a stroke as one undoable step.
void finishStroke(ToolsState& state, scene::Tilemap& tilemap)
{
    TilePainter& painter = state.tilePainter;
    painter.stroking = false;
    painter.rectangleStart.reset();
    const reflection::FieldInfo* const field = blocksField();
    if (field == nullptr)
    {
        return;
    }
    serialization::TextValue after = scene::writeFieldValue(*field, &tilemap.blocks);
    if (after != painter.before)
    {
        state.history.recordApplied(
            makeSetFieldCommand(painter.target, "Tilemap", "blocks", std::move(painter.before), std::move(after)));
    }
}

} // namespace

// The tools that paint the tilemap, under its card: the tool, how the tile is mirrored, the palette
// of the tiles of its tileset, and what the tool does.
struct TilePainterUi
{
    std::array<PanelButton, toolChoices.size()> tools;
    PanelButton flipX;
    PanelButton flipY;
    scene::Entity cells;
    scene::Entity message;
    scene::Entity hint;
    std::unique_ptr<SpriteGrid> palette;
    std::vector<std::uint32_t> tileIds;
};

void addTilePainter(InspectorUi& ui, EditorUiKit& kit, Section& section)
{
    auto painter = std::make_shared<TilePainterUi>();
    const scene::Entity title = ui.note(&section, "Paint", "text");
    ui.scene().get<scene::UiText>(title).font = EditorUiKit::boldFont();
    // The tools, in rows of three however narrow the panel.
    const scene::Entity tools = ui.add(section.card, "Tools", rects::wide(ui.line * 2.0f + ui.gap * 3.0f));
    ui.scene().add<scene::UiLayout>(tools, scene::UiLayout{.kind = scene::UiLayoutKind::Grid,
                                                           .spacing = ui.gap * 3.0f,
                                                           .padding = {ui.font * 0.35f, 0.0f, 0.0f, 0.0f},
                                                           .columns = 3,
                                                           .equalSize = true});
    section.lines.push_back(Line{.entity = tools});
    for (std::size_t index = 0; index < toolChoices.size(); ++index)
    {
        painter->tools[index] = ui.button(kit, tools, std::nullopt, toolChoices[index].label, "button", 0.0f, ui.line - 4.0f);
        ui.tooltip(painter->tools[index].entity, toolChoices[index].tooltip);
    }
    const scene::Entity flips = ui.actions(&section);
    painter->flipX = ui.action(kit, flips, std::nullopt, "Flip X");
    ui.tooltip(painter->flipX.entity, "Mirror the tile across");
    painter->flipY = ui.action(kit, flips, std::nullopt, "Flip Y");
    ui.tooltip(painter->flipY.entity, "Mirror the tile up and down");
    painter->cells = ui.text(flips, rects::middle({ui.font * 7.0f, ui.line}), "", "dim");
    painter->message = ui.note(&section, "", "dim");
    ui.spriteGrid(kit, section, painter->palette, 0, std::round(ui.font * 2.6f), false);
    painter->hint = ui.note(&section, "", "dim", 3.0f);
    section.painter = std::move(painter);
}

void syncTilePainter(InspectorUi& ui, ToolsState& state, EditorUiKit& kit, scene::Scene& edited, scene::Entity entity, Section& section)
{
    static_cast<void>(kit);
    TilePainterUi& shown = *section.painter;
    const scene::Tilemap* const tilemap = edited.tryGet<scene::Tilemap>(entity);
    if (tilemap == nullptr)
    {
        return;
    }
    TilePainter& painter = state.tilePainter;
    for (std::size_t index = 0; index < toolChoices.size(); ++index)
    {
        ui.scene().get<scene::UiRect>(shown.tools[index].entity).style = painter.tool == toolChoices[index].tool ? "primary" : "button";
    }
    ui.scene().get<scene::UiRect>(shown.flipX.entity).style = painter.flipX ? "primary" : "button";
    ui.scene().get<scene::UiRect>(shown.flipY.entity).style = painter.flipY ? "primary" : "button";
    const scene::TileGrid grid = scene::TileGrid::read(*tilemap);
    ui.scene().get<scene::UiText>(shown.cells).text = std::format("{} {}", grid.count(), grid.count() == 1 ? "cell" : "cells");

    const std::shared_ptr<const asset::TilesetData> tileset =
        state.tilesets && tilemap->tileset.isValid() ? state.tilesets(tilemap->tileset) : nullptr;
    const bool tiles = tileset != nullptr && !tileset->tiles.empty();
    ui.scene().get<scene::UiText>(shown.message).text = tileset == nullptr ? "Choose a tileset to paint with."
                                                        : !tiles           ? "The tileset has no tile yet: add some in its inspector."
                                                                           : "";
    ui.showLine(section, shown.message, !tiles);
    ui.showLine(section, shown.palette->grid, tiles);
    shown.tileIds.clear();
    if (tiles)
    {
        if (tileset->find(painter.tile) == nullptr)
        {
            painter.tile = tileset->tiles.front().id;
        }
        ui.gridCells(*shown.palette, tileset->tiles.size(), false);
        const double seconds = ImGui::GetTime();
        for (std::size_t index = 0; index < tileset->tiles.size(); ++index)
        {
            const asset::TileData& tile = tileset->tiles[index];
            ui.showSprite(shown.palette->images[index], tile.spriteAt(seconds));
            ui.scene().get<scene::UiRect>(shown.palette->cells[index]).style = tile.id == painter.tile ? "row_selected" : "row";
            const asset::AssetInfo* const info = state.database != nullptr ? state.database->find(tile.sprite) : nullptr;
            ui.tooltip(shown.palette->cells[index], std::format("Tile {}: {}", tile.id, info != nullptr ? info->name : std::string("(no sprite)")));
            shown.tileIds.push_back(tile.id);
        }
    }
    ui.scene().get<scene::UiText>(shown.hint).text =
        painter.tool == TileTool::None ? "Choose a tool or a tile to paint the tilemap in the view."
                                       : "Left drag paints in the view, Shift erases, Ctrl picks; right or middle drag slides the view; "
                                         "Escape stops painting. Each stroke is one step to undo.";
}

void answerTilePainter(InspectorUi& ui, ToolsState& state, Section& section)
{
    TilePainterUi& shown = *section.painter;
    TilePainter& painter = state.tilePainter;
    const ui::UiWorld& world = ui.panel.world();
    for (std::size_t index = 0; index < toolChoices.size(); ++index)
    {
        if (world.wasClicked(shown.tools[index].entity))
        {
            painter.tool = painter.tool == toolChoices[index].tool ? TileTool::None : toolChoices[index].tool;
        }
    }
    if (world.wasClicked(shown.flipX.entity))
    {
        painter.flipX = !painter.flipX;
    }
    if (world.wasClicked(shown.flipY.entity))
    {
        painter.flipY = !painter.flipY;
    }
    for (std::size_t index = 0; index < shown.tileIds.size() && index < shown.palette->cells.size(); ++index)
    {
        if (world.wasClicked(shown.palette->cells[index]))
        {
            painter.tile = shown.tileIds[index];
            if (painter.tool == TileTool::None || painter.tool == TileTool::Erase || painter.tool == TileTool::Pick)
            {
                painter.tool = TileTool::Paint;
            }
        }
    }
}

bool handleTilePainting(ToolsState& state, scene::Scene& scene, const ViewportView& view, math::Vec2 mouse, bool hovered)
{
    TilePainter& painter = state.tilePainter;
    scene::Entity entity;
    scene::Tilemap* const tilemap = paintedTilemap(state, scene, entity);
    if (painter.tool == TileTool::None || tilemap == nullptr || state.playState != PlayState::Editing)
    {
        painter.hovered.reset();
        painter.stroking = false;
        painter.rectangleStart.reset();
        return false;
    }
    if (state.viewportFocused && !painter.stroking && ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        painter.tool = TileTool::None;
        painter.hovered.reset();
        return false;
    }
    const math::Mat4 world = worldOf(scene, entity);
    const std::optional<math::IVec2> cell = cellUnder(*tilemap, world, view, mouse);
    painter.hovered = hovered || painter.stroking ? cell : std::nullopt;

    const ImGuiIO& io = ImGui::GetIO();
    TileTool tool = painter.tool;
    if (io.KeyCtrl && tool != TileTool::Fill)
    {
        tool = TileTool::Pick;
    }
    const bool erasing = tool == TileTool::Erase || (io.KeyShift && (tool == TileTool::Paint || tool == TileTool::Rectangle));
    const std::uint16_t value = erasing ? std::uint16_t{0} : brushValue(painter);

    if (!painter.stroking)
    {
        if (!hovered || !cell || !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            return hovered;
        }
        ImGui::SetWindowFocus();
        if (tool == TileTool::Pick)
        {
            const std::uint16_t picked = scene::tileAt(*tilemap, *cell);
            if (scene::tileIdOf(picked) != 0)
            {
                painter.tile = scene::tileIdOf(picked);
                painter.flipX = (picked & scene::tileFlipX) != 0;
                painter.flipY = (picked & scene::tileFlipY) != 0;
                painter.tool = TileTool::Paint;
            }
            return true;
        }
        const reflection::FieldInfo* const field = blocksField();
        if (field == nullptr)
        {
            return true;
        }
        painter.stroking = true;
        painter.target = scene.uuid(entity);
        painter.before = scene::writeFieldValue(*field, &tilemap->blocks);
        painter.lastCell = *cell;
        if (tool == TileTool::Fill)
        {
            scene::TileGrid grid = scene::TileGrid::read(*tilemap);
            floodFill(grid, *cell, value);
            grid.write(*tilemap);
            finishStroke(state, *tilemap);
            return true;
        }
        if (tool == TileTool::Rectangle)
        {
            painter.rectangleStart = *cell;
            return true;
        }
        scene::setTile(*tilemap, *cell, value);
        return true;
    }

    // The stroke goes on while the button is held, and ends as one step.
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        if (cell && *cell != painter.lastCell)
        {
            if (!painter.rectangleStart)
            {
                scene::TileGrid grid = scene::TileGrid::read(*tilemap);
                visitLine(painter.lastCell, *cell, [&](math::IVec2 visited) { grid.set(visited, value); });
                grid.write(*tilemap);
            }
            painter.lastCell = *cell;
        }
        return true;
    }
    if (painter.rectangleStart)
    {
        const math::IVec2 low = math::min(*painter.rectangleStart, painter.lastCell);
        const math::IVec2 high = math::max(*painter.rectangleStart, painter.lastCell);
        scene::TileGrid grid = scene::TileGrid::read(*tilemap);
        for (std::int32_t y = low.y; y <= high.y; ++y)
        {
            for (std::int32_t x = low.x; x <= high.x; ++x)
            {
                grid.set({x, y}, value);
            }
        }
        grid.write(*tilemap);
    }
    finishStroke(state, *tilemap);
    return true;
}

void addTilePainterOverlay(ToolsState& state, scene::Scene& scene, render::RenderWorld& world)
{
    const TilePainter& painter = state.tilePainter;
    scene::Entity entity;
    const scene::Tilemap* const tilemap = paintedTilemap(state, scene, entity);
    if (painter.tool == TileTool::None || tilemap == nullptr || !painter.hovered)
    {
        return;
    }
    const math::Mat4 cells = worldOf(scene, entity) * math::scale(math::Mat4{1.0f}, math::Vec3{tilemap->cellSize, 1.0f});
    const auto at = [&](float x, float y) { return math::Vec3(cells * math::Vec4(x, y, 0.0f, 1.0f)); };
    const auto line = [&](math::Vec3 from, math::Vec3 to, math::Vec4 color) {
        world.overlayLines.push_back({from, color});
        world.overlayLines.push_back({to, color});
    };

    // The cells around the mouse, fading out.
    constexpr std::int32_t reach = 10;
    const math::IVec2 center = *painter.hovered;
    for (std::int32_t offset = -reach; offset <= reach + 1; ++offset)
    {
        const float fade = 0.22f * (1.0f - static_cast<float>(std::abs(offset)) / static_cast<float>(reach + 2));
        const math::Vec4 color{1.0f, 1.0f, 1.0f, fade};
        const auto x = static_cast<float>(center.x + offset);
        const auto y = static_cast<float>(center.y + offset);
        line(at(x, static_cast<float>(center.y - reach)), at(x, static_cast<float>(center.y + reach + 1)), color);
        line(at(static_cast<float>(center.x - reach), y), at(static_cast<float>(center.x + reach + 1), y), color);
    }

    // The cells the tool is about to change.
    const bool erasing = painter.tool == TileTool::Erase || ImGui::GetIO().KeyShift;
    math::IVec2 low = center;
    math::IVec2 high = center;
    if (painter.rectangleStart)
    {
        low = math::min(*painter.rectangleStart, painter.lastCell);
        high = math::max(*painter.rectangleStart, painter.lastCell);
    }
    const math::Vec4 outline = erasing ? math::Vec4{1.0f, 0.3f, 0.25f, 1.0f} : math::Vec4{1.0f, 0.6f, 0.1f, 1.0f};
    const auto lowX = static_cast<float>(low.x);
    const auto lowY = static_cast<float>(low.y);
    const auto highX = static_cast<float>(high.x + 1);
    const auto highY = static_cast<float>(high.y + 1);
    line(at(lowX, lowY), at(highX, lowY), outline);
    line(at(highX, lowY), at(highX, highY), outline);
    line(at(highX, highY), at(lowX, highY), outline);
    line(at(lowX, highY), at(lowX, lowY), outline);

    // The tile itself, faint, over everything else.
    if (erasing || painter.tool == TileTool::Pick || painter.tool == TileTool::Fill || !state.tilesets || !state.sprites ||
        !state.textures)
    {
        return;
    }
    const std::shared_ptr<const asset::TilesetData> tileset = state.tilesets(tilemap->tileset);
    const asset::TileData* const tile = tileset != nullptr ? tileset->find(painter.tile) : nullptr;
    const std::shared_ptr<const asset::SpriteData> sprite = tile != nullptr ? state.sprites(tile->sprite) : nullptr;
    const render::TextureHandle texture = sprite != nullptr ? state.textures(sprite->texture) : render::TextureHandle{};
    if (!texture.isValid())
    {
        return;
    }
    math::Vec4 uv = sprite->uvRect();
    if (painter.flipX)
    {
        std::swap(uv.x, uv.z);
    }
    if (painter.flipY)
    {
        std::swap(uv.y, uv.w);
    }
    for (std::int32_t y = low.y; y <= high.y; ++y)
    {
        for (std::int32_t x = low.x; x <= high.x; ++x)
        {
            world.sprites.push_back({
                .transform = cells * math::translate(math::Mat4{1.0f}, math::Vec3{static_cast<float>(x), static_cast<float>(y), 0.01f}),
                .size = math::Vec2{1.0f},
                .pivot = math::Vec2{0.0f},
                .uvRect = uv,
                .naturalSize = math::Vec2{1.0f},
                .color = math::Vec4{1.0f, 1.0f, 1.0f, 0.6f},
                .texture = texture,
                .layer = 1 << 20,
            });
        }
    }
}

} // namespace devex::tools::detail
