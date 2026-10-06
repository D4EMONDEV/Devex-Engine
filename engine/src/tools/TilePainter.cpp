#include "InspectorUi.hpp"

#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/scene/TileTerrain.hpp>
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
    ToolChoice{TileTool::Pick, "Pick", "Take the tile or the terrain of the cell clicked, then paint with it"},
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

[[nodiscard]] std::shared_ptr<const asset::TilesetData> tilesetOf(const ToolsState& state, const scene::Tilemap& tilemap)
{
    return state.tilesets && tilemap.tileset.isValid() ? state.tilesets(tilemap.tileset) : nullptr;
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

// The same through the sides of the cells only, so that a path of terrain turns at right angles
// rather than touching by a corner.
template <typename Visit>
void visitSteps(math::IVec2 from, math::IVec2 to, Visit&& visit)
{
    const math::IVec2 delta = math::abs(to - from);
    const math::IVec2 step{to.x > from.x ? 1 : -1, to.y > from.y ? 1 : -1};
    math::IVec2 cell = from;
    visit(cell);
    for (std::int32_t x = 0, y = 0; x < delta.x || y < delta.y;)
    {
        // Steps along the axis that lags behind the straight line.
        if ((1 + 2 * x) * delta.y < (1 + 2 * y) * delta.x)
        {
            cell.x += step.x;
            ++x;
        }
        else
        {
            cell.y += step.y;
            ++y;
        }
        visit(cell);
    }
}

[[nodiscard]] bool outside(math::IVec2 cell, const std::pair<math::IVec2, math::IVec2>& bounds) noexcept
{
    return cell.x < bounds.first.x || cell.y < bounds.first.y || cell.x > bounds.second.x || cell.y > bounds.second.y;
}

// The cells like `start` that touch it through their sides. Empty cells are taken within the
// rectangle of the painted ones only, which bounds the fill.
template <typename Same>
[[nodiscard]] std::vector<math::IVec2> floodRegion(const scene::TileGrid& grid, math::IVec2 start, Same&& same)
{
    const std::optional<std::pair<math::IVec2, math::IVec2>> bounds = grid.bounds();
    const bool empty = grid.at(start) == 0;
    if (empty && (!bounds || outside(start, *bounds)))
    {
        return {start};
    }
    std::vector<math::IVec2> region;
    std::deque<math::IVec2> pending{start};
    std::set<std::pair<std::int32_t, std::int32_t>> seen{{start.x, start.y}};
    while (!pending.empty() && region.size() < maxFilledCells)
    {
        const math::IVec2 cell = pending.front();
        pending.pop_front();
        region.push_back(cell);
        for (const math::IVec2 offset : {math::IVec2{1, 0}, math::IVec2{-1, 0}, math::IVec2{0, 1}, math::IVec2{0, -1}})
        {
            const math::IVec2 next = cell + offset;
            if (empty && bounds && outside(next, *bounds))
            {
                continue;
            }
            if (same(grid.at(next)) && seen.insert({next.x, next.y}).second)
            {
                pending.push_back(next);
            }
        }
    }
    return region;
}

// Replaces the cells like `start` that touch it with a tile.
void floodFill(scene::TileGrid& grid, math::IVec2 start, std::uint16_t value)
{
    const std::uint16_t replaced = grid.at(start);
    if (replaced == value)
    {
        return;
    }
    for (const math::IVec2 cell : floodRegion(grid, start, [replaced](std::uint16_t other) { return other == replaced; }))
    {
        grid.set(cell, value);
    }
}

// The cells of the same terrain as `start` that touch it, painted with a terrain: the same tile for
// a tile outside the terrains.
[[nodiscard]] std::vector<math::IVec2> terrainRegion(const scene::TileGrid& grid, const asset::TilesetData& tileset, math::IVec2 start)
{
    const std::uint16_t first = grid.at(start);
    const std::pair<std::int32_t, std::int32_t> kind = scene::terrainOf(tileset, first);
    return floodRegion(grid, start, [&](std::uint16_t other) {
        if (other == 0 || first == 0)
        {
            return other == first;
        }
        return kind.first != asset::noTerrain ? scene::terrainOf(tileset, other) == kind : other == first;
    });
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

// The tile that shows a terrain best: one of it on every side, else any of it.
[[nodiscard]] const asset::TileData* terrainTile(const asset::TilesetData& tileset, std::int32_t set, std::int32_t terrain)
{
    const asset::TerrainSetData* const terrains = tileset.terrainSet(set);
    const asset::TileData* any = nullptr;
    for (const asset::TileData& tile : tileset.tiles)
    {
        if (terrains == nullptr || tile.terrainSet != set || tile.terrain != terrain)
        {
            continue;
        }
        any = any != nullptr ? any : &tile;
        bool whole = true;
        for (std::size_t index = 0; index < asset::tileNeighborCount; ++index)
        {
            whole = whole && (!asset::matchesNeighbor(terrains->mode, static_cast<asset::TileNeighbor>(index)) ||
                              tile.terrainBits[index] == terrain);
        }
        if (whole)
        {
            return &tile;
        }
    }
    return any;
}

[[nodiscard]] std::string_view modeLabel(asset::TerrainMode mode) noexcept
{
    switch (mode)
    {
    case asset::TerrainMode::CornersAndSides:
        return "corners and sides";
    case asset::TerrainMode::Corners:
        return "corners";
    case asset::TerrainMode::Sides:
        return "sides";
    }
    return "";
}

} // namespace

// The tools that paint the tilemap, under its card: the tool, tiles or terrains, how a tile is
// mirrored or how a terrain joins, the palette of the tiles or of the terrains of its tileset, and
// what the tool does.
struct TilePainterUi
{
    std::array<PanelButton, toolChoices.size()> tools;
    PanelButton tilesTab;
    PanelButton terrainsTab;
    scene::Entity cells;
    scene::Entity flips;
    PanelButton flipX;
    PanelButton flipY;
    scene::Entity joins;
    PanelButton connect;
    PanelButton path;
    scene::Entity message;
    scene::Entity hint;
    std::unique_ptr<SpriteGrid> palette;
    std::vector<std::uint32_t> tileIds;
    std::unique_ptr<SpriteGrid> terrainPalette;
    // The colour of each terrain, under its tile.
    std::vector<scene::Entity> terrainColors;
    std::vector<std::pair<std::int32_t, std::int32_t>> terrainIds;
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
    const scene::Entity modes = ui.actions(&section);
    painter->tilesTab = ui.action(kit, modes, std::nullopt, "Tiles");
    ui.tooltip(painter->tilesTab.entity, "Paint the tiles themselves");
    painter->terrainsTab = ui.action(kit, modes, std::nullopt, "Terrains");
    ui.tooltip(painter->terrainsTab.entity, "Paint terrains: each cell takes the tile that matches the cells around it, and they follow");
    painter->cells = ui.text(modes, rects::middle({ui.font * 7.0f, ui.line}), "", "dim");
    painter->flips = ui.actions(&section);
    painter->flipX = ui.action(kit, painter->flips, std::nullopt, "Flip X");
    ui.tooltip(painter->flipX.entity, "Mirror the tile across");
    painter->flipY = ui.action(kit, painter->flips, std::nullopt, "Flip Y");
    ui.tooltip(painter->flipY.entity, "Mirror the tile up and down");
    painter->joins = ui.actions(&section);
    painter->connect = ui.action(kit, painter->joins, std::nullopt, "Connect");
    ui.tooltip(painter->connect.entity, "The cells painted join each other and the cells of the same terrain around them");
    painter->path = ui.action(kit, painter->joins, std::nullopt, "Path");
    ui.tooltip(painter->path.entity, "Each cell of a stroke joins only the one before it and the one after it: roads, rivers");
    painter->message = ui.note(&section, "", "dim", 2.0f);
    ui.spriteGrid(kit, section, painter->palette, 0, std::round(ui.font * 2.6f), false);
    ui.spriteGrid(kit, section, painter->terrainPalette, 0, std::round(ui.font * 2.6f), false);
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
    const auto styleOf = [&](const PanelButton& button, bool on) {
        ui.scene().get<scene::UiRect>(button.entity).style = on ? "primary" : "button";
    };
    for (std::size_t index = 0; index < toolChoices.size(); ++index)
    {
        styleOf(shown.tools[index], painter.tool == toolChoices[index].tool);
    }
    styleOf(shown.tilesTab, !painter.terrains);
    styleOf(shown.terrainsTab, painter.terrains);
    styleOf(shown.flipX, painter.flipX);
    styleOf(shown.flipY, painter.flipY);
    styleOf(shown.connect, !painter.path);
    styleOf(shown.path, painter.path);
    ui.showLine(section, shown.flips, !painter.terrains);
    ui.showLine(section, shown.joins, painter.terrains);
    const scene::TileGrid grid = scene::TileGrid::read(*tilemap);
    ui.scene().get<scene::UiText>(shown.cells).text = std::format("{} {}", grid.count(), grid.count() == 1 ? "cell" : "cells");

    const std::shared_ptr<const asset::TilesetData> tileset = tilesetOf(state, *tilemap);
    const double seconds = state.input.time();
    shown.tileIds.clear();
    shown.terrainIds.clear();
    if (!painter.terrains)
    {
        const bool tiles = tileset != nullptr && !tileset->tiles.empty();
        ui.scene().get<scene::UiText>(shown.message).text = tileset == nullptr ? "Choose a tileset to paint with."
                                                            : !tiles           ? "The tileset has no tile yet: add some in its inspector."
                                                                               : "";
        ui.showLine(section, shown.message, !tiles);
        ui.showLine(section, shown.palette->grid, tiles);
        ui.showLine(section, shown.terrainPalette->grid, false);
        if (tiles)
        {
            if (tileset->find(painter.tile) == nullptr)
            {
                painter.tile = tileset->tiles.front().id;
            }
            ui.gridCells(*shown.palette, tileset->tiles.size(), false);
            for (std::size_t index = 0; index < tileset->tiles.size(); ++index)
            {
                const asset::TileData& tile = tileset->tiles[index];
                ui.showSprite(shown.palette->images[index], tile.spriteAt(seconds));
                ui.scene().get<scene::UiRect>(shown.palette->cells[index]).style = tile.id == painter.tile ? "row_selected" : "row";
                const asset::AssetInfo* const info = state.database != nullptr ? state.database->find(tile.sprite) : nullptr;
                ui.tooltip(shown.palette->cells[index],
                           std::format("Tile {}: {}", tile.id, info != nullptr ? info->name : std::string("(no sprite)")));
                shown.tileIds.push_back(tile.id);
            }
        }
    }
    else
    {
        if (tileset != nullptr)
        {
            for (std::size_t set = 0; set < tileset->terrainSets.size(); ++set)
            {
                for (std::size_t terrain = 0; terrain < tileset->terrainSets[set].terrains.size(); ++terrain)
                {
                    shown.terrainIds.emplace_back(static_cast<std::int32_t>(set), static_cast<std::int32_t>(terrain));
                }
            }
        }
        const bool terrains = !shown.terrainIds.empty();
        ui.scene().get<scene::UiText>(shown.message).text =
            tileset == nullptr ? "Choose a tileset to paint with."
            : !terrains        ? "The tileset has no terrain yet: add a terrain set in its inspector, then paint its terrains on the tiles."
                               : "";
        ui.showLine(section, shown.message, !terrains);
        ui.showLine(section, shown.palette->grid, false);
        ui.showLine(section, shown.terrainPalette->grid, terrains);
        if (terrains)
        {
            if (tileset->terrain(painter.terrainSet, painter.terrain) == nullptr)
            {
                painter.terrainSet = shown.terrainIds.front().first;
                painter.terrain = shown.terrainIds.front().second;
            }
            SpriteGrid& palette = *shown.terrainPalette;
            ui.gridCells(palette, shown.terrainIds.size(), false);
            while (shown.terrainColors.size() < palette.cells.size())
            {
                const scene::Entity cell = palette.cells[shown.terrainColors.size()];
                const float inset = std::round(palette.size * 0.08f);
                const scene::Entity color = ui.add(cell, "Color",
                                                   scene::UiRect{.anchorMin = {0.0f, 1.0f},
                                                                 .anchorMax = {1.0f, 1.0f},
                                                                 .offsetMin = {inset, -inset - std::round(palette.size * 0.12f)},
                                                                 .offsetMax = {-inset, -inset}});
                ui.scene().add<scene::UiImage>(color, scene::UiImage{.raycastTarget = false});
                shown.terrainColors.push_back(color);
            }
            for (std::size_t index = 0; index < shown.terrainIds.size(); ++index)
            {
                const auto [set, terrain] = shown.terrainIds[index];
                const asset::TerrainData& data = *tileset->terrain(set, terrain);
                const asset::TileData* const tile = terrainTile(*tileset, set, terrain);
                ui.showSprite(palette.images[index], tile != nullptr ? tile->spriteAt(seconds) : asset::AssetId{});
                ui.scene().get<scene::UiImage>(shown.terrainColors[index]).color = data.color;
                const bool chosen = set == painter.terrainSet && terrain == painter.terrain;
                ui.scene().get<scene::UiRect>(palette.cells[index]).style = chosen ? "row_selected" : "row";
                ui.tooltip(palette.cells[index],
                           std::format("{}\nTerrain set {}, matching {}{}", data.name.empty() ? std::string("(unnamed)") : data.name, set,
                                       modeLabel(tileset->terrainSets[static_cast<std::size_t>(set)].mode),
                                       tile == nullptr ? "\nNo tile shows it yet" : ""));
            }
        }
    }
    ui.scene().get<scene::UiText>(shown.hint).text =
        painter.tool == TileTool::None ? (painter.terrains ? "Choose a tool or a terrain to paint the tilemap in the view."
                                                           : "Choose a tool or a tile to paint the tilemap in the view.")
        : painter.terrains ? "Left drag paints the terrain in the view, Shift erases, Ctrl picks a terrain: each cell takes the tile "
                             "that matches its neighbours, and they follow. Escape stops painting. Each stroke is one step to undo."
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
    if (world.wasClicked(shown.tilesTab.entity))
    {
        painter.terrains = false;
    }
    if (world.wasClicked(shown.terrainsTab.entity))
    {
        painter.terrains = true;
    }
    if (world.wasClicked(shown.flipX.entity))
    {
        painter.flipX = !painter.flipX;
    }
    if (world.wasClicked(shown.flipY.entity))
    {
        painter.flipY = !painter.flipY;
    }
    if (world.wasClicked(shown.connect.entity))
    {
        painter.path = false;
    }
    if (world.wasClicked(shown.path.entity))
    {
        painter.path = true;
    }
    const auto paintWith = [&] {
        if (painter.tool == TileTool::None || painter.tool == TileTool::Erase || painter.tool == TileTool::Pick)
        {
            painter.tool = TileTool::Paint;
        }
    };
    for (std::size_t index = 0; index < shown.tileIds.size() && index < shown.palette->cells.size(); ++index)
    {
        if (world.wasClicked(shown.palette->cells[index]))
        {
            painter.tile = shown.tileIds[index];
            paintWith();
        }
    }
    for (std::size_t index = 0; index < shown.terrainIds.size() && index < shown.terrainPalette->cells.size(); ++index)
    {
        if (world.wasClicked(shown.terrainPalette->cells[index]))
        {
            painter.terrainSet = shown.terrainIds[index].first;
            painter.terrain = shown.terrainIds[index].second;
            paintWith();
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
    if (state.viewportFocused && !painter.stroking && state.input.pressed(platform::Key::Escape, true))
    {
        painter.tool = TileTool::None;
        painter.hovered.reset();
        return false;
    }
    const math::Mat4 world = worldOf(scene, entity);
    const std::optional<math::IVec2> cell = cellUnder(*tilemap, world, view, mouse);
    painter.hovered = hovered || painter.stroking ? cell : std::nullopt;

    TileTool tool = painter.tool;
    if (state.input.ctrl() && tool != TileTool::Fill)
    {
        tool = TileTool::Pick;
    }
    const bool erasing = tool == TileTool::Erase || (state.input.shift() && (tool == TileTool::Paint || tool == TileTool::Rectangle));
    const std::uint16_t value = erasing ? std::uint16_t{0} : brushValue(painter);
    // Terrains paint with the terrain of the brush, or empty the cells, and the cells around follow.
    const std::shared_ptr<const asset::TilesetData> tileset = painter.terrains ? tilesetOf(state, *tilemap) : nullptr;
    const bool terrains = tileset != nullptr && tileset->terrainSet(painter.terrainSet) != nullptr;
    if (painter.terrains && !terrains)
    {
        return hovered;
    }
    const std::int32_t terrain = erasing ? asset::noTerrain : painter.terrain;
    const auto paintCells = [&](scene::TileGrid& grid, std::span<const math::IVec2> cells, scene::TerrainPaint mode) {
        static_cast<void>(scene::paintTerrain(grid, *tileset, cells, painter.terrainSet, terrain, mode));
    };

    if (!painter.stroking)
    {
        if (!hovered || !cell || !state.input.clicked(Mouse::Left))
        {
            return hovered;
        }
        state.hosts.focusCurrent();
        if (tool == TileTool::Pick)
        {
            const std::uint16_t picked = scene::tileAt(*tilemap, *cell);
            if (terrains)
            {
                const auto [set, kind] = scene::terrainOf(*tileset, picked);
                if (set != asset::noTerrain && kind != asset::noTerrain)
                {
                    painter.terrainSet = set;
                    painter.terrain = kind;
                    painter.tool = TileTool::Paint;
                }
            }
            else if (scene::tileIdOf(picked) != 0)
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
            if (terrains)
            {
                paintCells(grid, terrainRegion(grid, *tileset, *cell), scene::TerrainPaint::Connect);
            }
            else
            {
                floodFill(grid, *cell, value);
            }
            grid.write(*tilemap);
            finishStroke(state, *tilemap);
            return true;
        }
        if (tool == TileTool::Rectangle)
        {
            painter.rectangleStart = *cell;
            return true;
        }
        if (terrains)
        {
            scene::TileGrid grid = scene::TileGrid::read(*tilemap);
            paintCells(grid, std::span(&*cell, 1), scene::TerrainPaint::Connect);
            grid.write(*tilemap);
        }
        else
        {
            scene::setTile(*tilemap, *cell, value);
        }
        return true;
    }

    // The stroke goes on while the button is held, and ends as one step.
    if (state.input.down(Mouse::Left))
    {
        if (cell && *cell != painter.lastCell)
        {
            if (!painter.rectangleStart)
            {
                scene::TileGrid grid = scene::TileGrid::read(*tilemap);
                if (terrains)
                {
                    // A path goes on from the cell it reached, which joins the next one.
                    std::vector<math::IVec2> cells;
                    const auto add = [&](math::IVec2 visited) { cells.push_back(visited); };
                    if (painter.path)
                    {
                        visitSteps(painter.lastCell, *cell, add);
                    }
                    else
                    {
                        visitLine(painter.lastCell, *cell, add);
                    }
                    paintCells(grid, cells, painter.path ? scene::TerrainPaint::Path : scene::TerrainPaint::Connect);
                }
                else
                {
                    visitLine(painter.lastCell, *cell, [&](math::IVec2 visited) { grid.set(visited, value); });
                }
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
        std::vector<math::IVec2> cells;
        for (std::int32_t y = low.y; y <= high.y; ++y)
        {
            for (std::int32_t x = low.x; x <= high.x; ++x)
            {
                cells.push_back({x, y});
            }
        }
        if (terrains)
        {
            paintCells(grid, cells, scene::TerrainPaint::Connect);
        }
        else
        {
            for (const math::IVec2 filled : cells)
            {
                grid.set(filled, value);
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
    const bool erasing = painter.tool == TileTool::Erase || state.input.shift();
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

    // The tiles themselves, faint, over everything else.
    if (painter.tool == TileTool::Pick || painter.tool == TileTool::Fill || !state.tilesets || !state.sprites || !state.textures)
    {
        return;
    }
    const std::shared_ptr<const asset::TilesetData> tileset = tilesetOf(state, *tilemap);
    if (tileset == nullptr)
    {
        return;
    }
    const auto showTile = [&](math::IVec2 cell, std::uint16_t value) {
        const asset::TileData* const tile = tileset->find(scene::tileIdOf(value));
        const std::shared_ptr<const asset::SpriteData> sprite = tile != nullptr ? state.sprites(tile->sprite) : nullptr;
        const render::TextureHandle texture = sprite != nullptr ? state.textures(sprite->texture) : render::TextureHandle{};
        if (!texture.isValid())
        {
            return;
        }
        math::Vec4 uv = sprite->uvRect();
        if ((value & scene::tileFlipX) != 0)
        {
            std::swap(uv.x, uv.z);
        }
        if ((value & scene::tileFlipY) != 0)
        {
            std::swap(uv.y, uv.w);
        }
        world.sprites.push_back({
            .transform = cells * math::translate(math::Mat4{1.0f}, math::Vec3{static_cast<float>(cell.x), static_cast<float>(cell.y), 0.01f}),
            .size = math::Vec2{1.0f},
            .pivot = math::Vec2{0.0f},
            .uvRect = uv,
            .naturalSize = math::Vec2{1.0f},
            .color = math::Vec4{1.0f, 1.0f, 1.0f, 0.6f},
            .texture = texture,
            .layer = 1 << 20,
        });
    };
    if (painter.terrains)
    {
        // What the brush would paint, and how the cells around would follow, unless a stroke paints
        // already.
        if (painter.stroking && !painter.rectangleStart)
        {
            return;
        }
        if (tileset->terrainSet(painter.terrainSet) == nullptr)
        {
            return;
        }
        scene::TileGrid preview = scene::TileGrid::read(*tilemap);
        std::vector<math::IVec2> painted;
        for (std::int32_t y = low.y; y <= high.y; ++y)
        {
            for (std::int32_t x = low.x; x <= high.x; ++x)
            {
                painted.push_back({x, y});
            }
        }
        for (const math::IVec2 cell :
             scene::paintTerrain(preview, *tileset, painted, painter.terrainSet, erasing ? asset::noTerrain : painter.terrain))
        {
            showTile(cell, preview.at(cell));
        }
        return;
    }
    if (erasing)
    {
        return;
    }
    for (std::int32_t y = low.y; y <= high.y; ++y)
    {
        for (std::int32_t x = low.x; x <= high.x; ++x)
        {
            showTile({x, y}, brushValue(painter));
        }
    }
}

} // namespace devex::tools::detail
