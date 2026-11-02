#include "TileCollisionEditor.hpp"

#include "InspectorUi.hpp"

#include <devex/asset/SpriteData.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace devex::tools::detail
{
namespace
{

std::vector<math::Vec2> rectangle() { return {{0, 0}, {1, 0}, {1, 1}, {0, 1}}; }

} // namespace

void TileCollisionEditor::remember()
{
    if (m_undo.size() == 64)
        m_undo.erase(m_undo.begin());
    if (m_undo.empty() || m_undo.back() != m_draft)
        m_undo.push_back(m_draft);
    m_redo.clear();
}

void TileCollisionEditor::restore(const asset::TileData& tile)
{
    m_draft = {.points = tile.collisionPoints, .oneWay = tile.collisionOneWay};
    m_saved = m_draft;
    m_undo.clear();
    m_redo.clear();
    m_drag.reset();
    m_selected.reset();
}

bool TileCollisionEditor::changed() const { return m_draft != m_saved; }

void TileCollisionEditor::build(InspectorUi& ui, EditorUiKit& kit, Section& section,
                                asset::AssetId asset, const asset::TileData& tile)
{
    m_drag.reset();
    section.filterTogether = true;
    if (m_asset != asset || m_tile != tile.id || m_saved.points != tile.collisionPoints ||
        m_saved.oneWay != tile.collisionOneWay)
    {
        m_asset = asset;
        m_tile = tile.id;
        restore(tile);
    }
    ui.note(&section,
            "Draw points around the solid part of the tile. Every painted copy uses this shape.",
            "dim", 2.0f);
    const auto drawActions = ui.actions(&section);
    ui.scene().get<scene::UiLayout>(drawActions).equalSize = true;
    m_draw = ui.action(kit, drawActions, std::nullopt, "Draw");
    m_rectangle = ui.action(kit, drawActions, std::nullopt, "Rectangle");
    m_finish = ui.action(kit, drawActions, std::nullopt, "Finish");
    ui.tooltip(m_draw.entity,
               "Starts a new outline: click to place points, then click the first point or Finish");
    ui.tooltip(m_rectangle.entity, "Starts from a rectangle covering the whole cell");
    ui.tooltip(m_finish.entity, "Closes the outline; Apply saves it to the tileset");

    m_preview = ui.add(section.card, "Collision Preview", rects::wide(m_side + ui.pad * 4));
    section.lines.push_back({.entity = m_preview});
    m_canvas = ui.add(m_preview, "Collision Canvas",
                      scene::UiRect{.anchorMin = {0.5f, 0}, .anchorMax = {0.5f, 0}});
    ui.scene().add<scene::UiImage>(m_canvas, scene::UiImage{.texture = kit.checker()});
    ui.scene().add<scene::UiButton>(m_canvas);
    ui.tooltip(m_canvas, "Drag a vertex to move it. Click an edge to insert a vertex. Right-click "
                         "a vertex to remove it.");
    m_image = ui.add(m_canvas, "Tile Image", rects::whole());
    ui.scene().add<scene::UiImage>(
        m_image, scene::UiImage{.color = {1, 1, 1, 0.8f}, .raycastTarget = false});
    for (auto& grid : m_grid)
    {
        grid = ui.add(m_canvas, "Grid", rects::whole());
        ui.scene().add<scene::UiLine>(grid, scene::UiLine{.width = 1.0f});
    }
    m_outline = ui.add(m_canvas, "Collision Outline", rects::whole());
    ui.scene().add<scene::UiLine>(m_outline, scene::UiLine{.width = 2.0f});
    for (auto& handle : m_handles)
    {
        handle = ui.add(m_canvas, "Vertex", scene::UiRect{.visible = false});
        ui.scene().add<scene::UiImage>(handle,
                                       scene::UiImage{.cornerRadius = 2, .raycastTarget = false});
    }
    m_snap = ui.toggle(ui.formRow(section, "Snap to Pixels").editor);
    m_oneWay = ui.toggle(ui.formRow(section, "One Way").editor);
    ui.tooltip(m_oneWay, "Supports bodies landing from above, while letting them pass from below");
    m_status = ui.note(&section, "", "dim", 3.0f);
    const auto actions = ui.actions(&section);
    m_apply = ui.action(kit, actions, std::nullopt, "Apply", "primary");
    m_revert = ui.action(kit, actions, std::nullopt, "Revert");
    m_undoButton = ui.button(kit, actions, Icon::Undo, "", "button", ui.line, ui.line - 4.0f);
    m_redoButton = ui.button(kit, actions, Icon::Redo, "", "button", ui.line, ui.line - 4.0f);
    ui.tooltip(m_apply.entity, "Saves this collision outline in the tileset");
    ui.tooltip(m_revert.entity, "Discards the draft and restores the tile's saved collision");
    ui.tooltip(m_undoButton.entity, "Undo the last outline edit");
    ui.tooltip(m_redoButton.entity, "Redo the last outline edit");
}

void TileCollisionEditor::sync(InspectorUi& ui, ToolsState& state, const asset::TileData& tile)
{
    if (m_saved.points != tile.collisionPoints || m_saved.oneWay != tile.collisionOneWay)
        restore(tile);
    const auto& colors = themeColors();
    // The tile image fills the cell, just as the Tilemap stretches it to Cell Size at runtime.
    ui.showSprite(m_image, tile.spriteAt(0));
    m_pixels = {16, 16};
    if (state.sprites)
        if (const auto sprite = state.sprites(tile.spriteAt(0)))
            m_pixels = {static_cast<float>(std::max(sprite->width, 1u)),
                        static_cast<float>(std::max(sprite->height, 1u))};
    const float available =
        std::max(ui.panel.size().x - ui.font * 0.6f - 8.0f - ui.pad * 4.0f, 32.0f);
    m_side = std::min(available, ui.font * 20.0f);
    auto& preview = ui.scene().get<scene::UiRect>(m_preview);
    preview.offsetMax.y = preview.offsetMin.y + m_side + ui.pad * 4;
    auto& canvas = ui.scene().get<scene::UiRect>(m_canvas);
    canvas.offsetMin = {-m_side * 0.5f, ui.pad * 2};
    canvas.offsetMax = {m_side * 0.5f, ui.pad * 2 + m_side};
    for (std::size_t i = 0; i < 7; ++i)
    {
        const float at = static_cast<float>(i + 1) * m_side / 8;
        auto& vertical = ui.scene().get<scene::UiLine>(m_grid[i]);
        auto& horizontal = ui.scene().get<scene::UiLine>(m_grid[i + 7]);
        vertical.points = {{at, 0}, {at, m_side}};
        horizontal.points = {{0, at}, {m_side, at}};
        vertical.color = horizontal.color =
            linearColor(math::Vec4(colors.textDim.x, colors.textDim.y, colors.textDim.z, 0.2f));
    }
    const auto valid = asset::validateTileCollision(m_draft.points);
    const auto color = linearColor(valid && m_draft.closed ? colors.physics : colors.error);
    auto& outline = ui.scene().get<scene::UiLine>(m_outline);
    outline.points.clear();
    for (const auto point : m_draft.points)
        outline.points.push_back({point.x * m_side, (1 - point.y) * m_side});
    outline.closed = m_draft.closed;
    outline.color = color;
    const float half = std::max(std::round(ui.font * 0.32f), 3.0f);
    for (std::size_t i = 0; i < m_handles.size(); ++i)
    {
        auto& rect = ui.scene().get<scene::UiRect>(m_handles[i]);
        rect.visible = i < m_draft.points.size();
        if (!rect.visible)
            continue;
        rect.anchorMin = rect.anchorMax = {m_draft.points[i].x, 1 - m_draft.points[i].y};
        rect.offsetMin = {-half, -half};
        rect.offsetMax = {half, half};
        auto& image = ui.scene().get<scene::UiImage>(m_handles[i]);
        image.color = i == m_selected ? linearColor(colors.accent) : color;
        image.outlineColor = linearColor(colors.field);
        image.outlineWidth = 1;
    }
    ui.setToggle(m_snap, m_snapPixels);
    ui.setToggle(m_oneWay, m_draft.oneWay);
    std::string status;
    if (!m_draft.closed)
        status = "Click to add points; click the first point or Finish to close the outline.";
    else if (!valid)
        status = valid.error().message;
    else
        status = std::format("{} points. {}", m_draft.points.size(),
                             changed() ? "Draft: Apply to save." : "Saved in the tileset.");
    if (m_selected && *m_selected < m_draft.points.size())
    {
        const auto point = m_draft.points[*m_selected];
        status += std::format(" Point {}: ({:.3f}, {:.3f})", *m_selected + 1, point.x, point.y);
    }
    ui.scene().get<scene::UiText>(m_status).text = std::move(status);
    ui.scene().get<scene::UiRect>(m_status).style = valid || !m_draft.closed ? "dim" : "error";
    ui.enable(m_finish, !m_draft.closed && valid.has_value());
    ui.enable(m_apply, changed() && m_draft.closed && valid.has_value() && !m_drag);
    ui.enable(m_revert, changed());
    ui.enable(m_undoButton, !m_undo.empty());
    ui.enable(m_redoButton, !m_redo.empty());
}

bool TileCollisionEditor::answer(InspectorUi& ui, const ToolsState&, asset::TileData& tile)
{
    const auto& world = ui.panel.world();
    const auto& input = ui.panel.input();
    if (world.wasClicked(m_draw.entity))
    {
        remember();
        m_draft.points.clear();
        m_draft.closed = false;
        m_selected.reset();
    }
    if (world.wasClicked(m_rectangle.entity))
    {
        remember();
        m_draft.points = rectangle();
        m_draft.closed = true;
        m_selected.reset();
    }
    if (world.wasClicked(m_finish.entity) && asset::validateTileCollision(m_draft.points))
    {
        remember();
        m_draft.closed = true;
    }
    if (world.wasChanged(m_snap))
        m_snapPixels = ui.scene().get<scene::UiToggle>(m_snap).value;
    if (world.wasChanged(m_oneWay))
    {
        remember();
        m_draft.oneWay = ui.scene().get<scene::UiToggle>(m_oneWay).value;
    }
    if (world.wasClicked(m_undoButton.entity) && !m_undo.empty())
    {
        m_redo.push_back(m_draft);
        m_draft = std::move(m_undo.back());
        m_undo.pop_back();
        m_selected.reset();
    }
    if (world.wasClicked(m_redoButton.entity) && !m_redo.empty())
    {
        m_undo.push_back(m_draft);
        m_draft = std::move(m_redo.back());
        m_redo.pop_back();
        m_selected.reset();
    }
    if (world.wasClicked(m_revert.entity))
        restore(tile);

    const auto* rect =
        world.canvases().empty() ? nullptr : world.canvases().front().layout.find(m_canvas);
    if (rect != nullptr && rect->visible)
    {
        const math::Vec2 extent = math::max(rect->size(), math::Vec2{1});
        const math::Vec2 at = (input.pointer - rect->min) / extent;
        math::Vec2 point{std::clamp(at.x, 0.0f, 1.0f), std::clamp(1 - at.y, 0.0f, 1.0f)};
        if (m_snapPixels)
            point = math::Vec2{std::round(point.x * m_pixels.x), std::round(point.y * m_pixels.y)} /
                    m_pixels;
        std::optional<std::size_t> nearest;
        float distance = std::max(ui.font * 0.7f, 8.0f);
        for (std::size_t i = 0; i < m_draft.points.size(); ++i)
        {
            const auto offset =
                (math::Vec2{m_draft.points[i].x, 1 - m_draft.points[i].y} - at) * extent;
            const float next = math::length(offset);
            if (next <= distance)
            {
                nearest = i;
                distance = next;
            }
        }
        if (world.hovered() == m_canvas)
        {
            if (input.secondaryPressed && nearest)
            {
                remember();
                m_draft.points.erase(m_draft.points.begin() +
                                     static_cast<std::ptrdiff_t>(*nearest));
                m_selected.reset();
            }
            else if (input.pointerPressed)
            {
                if (!m_draft.closed)
                {
                    if (nearest == 0 && m_draft.points.size() >= 3)
                    {
                        remember();
                        m_draft.closed = true;
                    }
                    else if (m_draft.points.size() < asset::maxTileCollisionPoints)
                    {
                        remember();
                        m_draft.points.push_back(point);
                        m_selected = m_draft.points.size() - 1;
                    }
                }
                else if (nearest)
                {
                    remember();
                    m_selected = m_drag = nearest;
                }
                else if (m_draft.points.size() < asset::maxTileCollisionPoints)
                {
                    // Insert on the nearest edge; clicking elsewhere does not alter the outline.
                    for (std::size_t i = 0; i < m_draft.points.size(); ++i)
                    {
                        const auto a =
                            math::Vec2{m_draft.points[i].x, 1 - m_draft.points[i].y} * extent;
                        const auto b =
                            math::Vec2{m_draft.points[(i + 1) % m_draft.points.size()].x,
                                       1 - m_draft.points[(i + 1) % m_draft.points.size()].y} *
                            extent;
                        const auto ab = b - a;
                        const float t = std::clamp(math::dot(at * extent - a, ab) /
                                                       std::max(math::dot(ab, ab), 0.001f),
                                                   0.0f, 1.0f);
                        if (math::length(at * extent - (a + ab * t)) <= ui.font * 0.6f)
                        {
                            remember();
                            m_draft.points.insert(
                                m_draft.points.begin() + static_cast<std::ptrdiff_t>(i + 1), point);
                            m_selected = m_drag = i + 1;
                            break;
                        }
                    }
                }
            }
        }
        if (m_drag && *m_drag < m_draft.points.size() && input.pointerDown)
            m_draft.points[*m_drag] = point;
    }
    if (!input.pointerDown)
        m_drag.reset();
    if (world.wasClicked(m_apply.entity) && m_draft.closed &&
        asset::validateTileCollision(m_draft.points))
    {
        tile.collisionPoints = m_draft.points;
        tile.collisionOneWay = m_draft.oneWay;
        m_saved = m_draft;
        return true;
    }
    return false;
}

} // namespace devex::tools::detail
