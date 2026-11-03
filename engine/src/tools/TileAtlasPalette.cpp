#include "TileAtlasPalette.hpp"

#include "FormUi.hpp"
#include "ToolsState.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace devex::tools::detail
{
namespace
{

float fitScale(const TileAtlasView& view) noexcept
{
    return std::min(view.viewport.x / std::max(view.pixels.x, 1.0f),
                    view.viewport.y / std::max(view.pixels.y, 1.0f)) *
           0.95f;
}

scene::UiRect regionRect(const TileAtlasRegion& region, math::Vec2 pixels)
{
    return {.anchorMin = region.min / pixels,
            .anchorMax = region.max / pixels,
            .offsetMin = {0, 0},
            .offsetMax = {0, 0}};
}

std::string tileName(const ToolsState& state, const TileAtlasRegion& region)
{
    const auto* info = state.database ? state.database->find(region.sprite) : nullptr;
    return std::format("{}: {}", region.tile, info ? info->name : "(no sprite)");
}

} // namespace

std::uint32_t TileAtlasSource::tileAt(math::Vec2 pixel) const noexcept
{
    if (!texture.isValid())
        return 0;
    std::uint32_t result = 0;
    float smallest = std::numeric_limits<float>::max();
    for (const auto& region : regions)
    {
        const auto size = region.max - region.min;
        const float area = size.x * size.y;
        if (pixel.x >= region.min.x && pixel.y >= region.min.y && pixel.x < region.max.x &&
            pixel.y < region.max.y && area < smallest)
        {
            result = region.tile;
            smallest = area;
        }
    }
    return result;
}

std::vector<TileAtlasSource> makeTileAtlases(std::span<const asset::TileData> tiles,
                                             const PaletteSpriteSource& sprites)
{
    std::vector<TileAtlasSource> sources;
    for (const auto& tile : tiles)
    {
        const auto spriteId = tile.sprite.isValid() ? tile.sprite : tile.spriteAt(0);
        const auto sprite = sprites && spriteId.isValid() ? sprites(spriteId) : nullptr;
        const bool valid = sprite && sprite->texture.isValid() && sprite->width > 0 &&
                           sprite->height > 0 && sprite->textureWidth > 0 &&
                           sprite->textureHeight > 0 && sprite->x < sprite->textureWidth &&
                           sprite->y < sprite->textureHeight &&
                           sprite->width <= sprite->textureWidth - sprite->x &&
                           sprite->height <= sprite->textureHeight - sprite->y;
        const auto texture = valid ? sprite->texture : asset::AssetId{};
        auto found = std::ranges::find(sources, texture, &TileAtlasSource::texture);
        if (found == sources.end())
        {
            sources.push_back({.texture = texture});
            found = sources.end() - 1;
        }
        TileAtlasRegion region{.tile = tile.id, .sprite = spriteId};
        if (valid)
        {
            region.min = {sprite->x, sprite->y};
            region.max = region.min + math::Vec2{sprite->width, sprite->height};
            found->pixels =
                math::max(found->pixels, math::Vec2{sprite->textureWidth, sprite->textureHeight});
        }
        found->regions.push_back(region);
    }
    return sources;
}

void TileAtlasView::clamp() noexcept
{
    const auto size = pixels * scale;
    for (int axis = 0; axis < 2; ++axis)
        offset[axis] = size[axis] <= viewport[axis]
                           ? (viewport[axis] - size[axis]) * 0.5f
                           : std::clamp(offset[axis], viewport[axis] - size[axis], 0.0f);
}

void TileAtlasView::fit() noexcept
{
    scale = fitScale(*this);
    offset = (viewport - pixels * scale) * 0.5f;
    fitted = true;
}

void TileAtlasView::resize(math::Vec2 size) noexcept
{
    viewport = math::max(size, math::Vec2{1});
    if (fitted)
        fit();
    else
        clamp();
}

void TileAtlasView::zoom(float factor, math::Vec2 at) noexcept
{
    const auto pixel = pixelAt(at);
    scale = std::clamp(scale * factor, fitScale(*this) * 0.25f, std::max(32.0f, fitScale(*this)));
    offset = at - pixel * scale;
    fitted = false;
    clamp();
}

void TileAtlasView::pan(math::Vec2 delta) noexcept
{
    offset += delta;
    fitted = false;
    clamp();
}

math::Vec2 TileAtlasView::pixelAt(math::Vec2 at) const noexcept { return (at - offset) / scale; }

void TileAtlasPalette::build(FormUi& ui, EditorUiKit& kit, Section& section)
{
    const auto first = section.lines.size();
    const auto row = ui.formRow(section, "Source");
    m_sourceRow = row.row;
    m_sourceChoice = ui.choice(row.editor);
    ui.tooltip(m_sourceChoice, "The original image; tile positions and empty spaces are preserved");
    m_toolbar = ui.actions(&section);
    m_zoomOut = ui.button(kit, m_toolbar, Icon::Minus, "", "button", ui.line, ui.line - 4);
    m_percent = ui.text(m_toolbar, rects::middle({ui.font * 4.0f, ui.line}), "", "dim", false,
                        scene::TextAlign::Center);
    m_zoomIn = ui.button(kit, m_toolbar, Icon::Plus, "", "button", ui.line, ui.line - 4);
    m_fit = ui.action(kit, m_toolbar, std::nullopt, "Fit");
    m_grid = ui.action(kit, m_toolbar, std::nullopt, "Grid");
    ui.tooltip(m_zoomOut.entity, "Zoom out of the tile sheet");
    ui.tooltip(m_zoomIn.entity, "Zoom in on the tile sheet");
    ui.tooltip(m_fit.entity, "Show the whole source image");
    ui.tooltip(m_grid.entity, "Show tile boundaries over the continuous image");
    m_pane = ui.add(section.card, "Tile Atlas", rects::wide(ui.font * 18), "field");
    section.lines.push_back({.entity = m_pane});
    ui.scene().add<scene::UiImage>(m_pane);
    // Focus is needed for pointer hit-testing, but the canvas must never use button tints.
    ui.scene().add<scene::UiButton>(m_pane, scene::UiButton{.hoverColor = {1, 1, 1, 1},
                                                            .pressedColor = {1, 1, 1, 1},
                                                            .disabledColor = {1, 1, 1, 1},
                                                            .fadeTime = 0});
    // Capture the wheel here, so zooming never scrolls the inspector as well.
    ui.scene().add<scene::UiScroll>(
        m_pane,
        scene::UiScroll{.horizontal = false, .vertical = false, .speed = 0, .scrollbar = false});
    m_sheet = ui.add(m_pane, "Atlas Sheet", rects::whole());
    ui.scene().add<scene::UiImage>(
        m_sheet, scene::UiImage{.texture = kit.checker(), .raycastTarget = false});
    m_image = ui.add(m_sheet, "Source Image", rects::whole());
    ui.scene().add<scene::UiImage>(m_image, scene::UiImage{.raycastTarget = false});
    // Lines come before the selected and hovered outlines in drawing order.
    const auto guides = ui.add(m_sheet, "Tile Boundaries", rects::whole());
    m_gridLines.push_back(guides);
    for (auto* outline : {&m_hover, &m_selected})
    {
        *outline = ui.add(m_sheet, "Tile Outline", rects::whole());
        ui.scene().add<scene::UiLine>(*outline, scene::UiLine{.width = 2, .closed = true});
    }
    m_tileChoice = ui.choice(ui.formRow(section, "Tile").editor);
    ui.tooltip(m_tileChoice,
               "The selected tile; this list also lets you choose overlapping variants");
    ui.note(&section, "Click a tile to paint. Wheel: zoom. Middle or right drag: move the sheet.",
            "dim", 2);
    for (std::size_t i = first; i < section.lines.size(); ++i)
        m_lines.push_back(section.lines[i].entity);
}

void TileAtlasPalette::show(FormUi& ui, Section& section, bool visible)
{
    for (const auto entity : m_lines)
        ui.showLine(section, entity, visible);
}

const TileAtlasSource* TileAtlasPalette::source() const noexcept
{
    return m_source < m_sources.size() ? &m_sources[m_source] : nullptr;
}

void TileAtlasPalette::sync(FormUi& ui, Section& section, const ToolsState& state,
                            const std::shared_ptr<const asset::TilesetData>& tileset,
                            std::uint32_t tile)
{
    const auto previousSource = m_source;
    const auto previousTexture = source() ? source()->texture : asset::AssetId{};
    const auto previousPixels = source() ? source()->pixels : math::Vec2{0};
    const auto revision = state.database ? state.database->revision() : 0;
    const bool rebuilt = m_tileset != tileset || m_revision != revision ||
                         (m_missing && state.input.time() >= m_retryAt);
    if (rebuilt)
    {
        const auto previous = source() ? source()->texture : asset::AssetId{};
        m_sources = makeTileAtlases(tileset->tiles, state.sprites);
        m_tileset = tileset;
        m_revision = revision;
        m_retryAt = state.input.time() + 0.5;
        m_missing =
            std::ranges::any_of(m_sources,
                                [](const auto& item)
                                {
                                    return !item.texture.isValid() &&
                                           std::ranges::any_of(item.regions, [](const auto& region)
                                                               { return region.sprite.isValid(); });
                                });
        m_source = 0;
        std::vector<std::string> labels;
        for (std::size_t i = 0; i < m_sources.size(); ++i)
        {
            const auto& item = m_sources[i];
            if (item.texture == previous)
                m_source = i;
            const auto* info = state.database ? state.database->find(item.texture) : nullptr;
            labels.push_back(!item.texture.isValid() ? "Tiles without an available sprite"
                             : info                  ? info->name
                                                     : item.texture.uuid.toString());
        }
        ui.setChoice(m_sourceChoice, std::move(labels), static_cast<std::int32_t>(m_source));
    }
    // Picking in the viewport also follows the selected tile to the correct source image.
    if (rebuilt || tile != m_lastTile)
    {
        for (std::size_t i = 0; i < m_sources.size(); ++i)
            if (std::ranges::find(m_sources[i].regions, tile, &TileAtlasRegion::tile) !=
                m_sources[i].regions.end())
            {
                const bool changed = previousTexture != m_sources[i].texture ||
                                     previousPixels != m_sources[i].pixels;
                m_source = i;
                if (changed)
                {
                    m_view.pixels = math::max(m_sources[i].pixels, math::Vec2{1});
                    m_view.fit();
                    m_hovered.reset();
                    m_panning.reset();
                }
                break;
            }
        m_lastTile = tile;
    }
    ui.scene().get<scene::UiDropdown>(m_sourceChoice).selected =
        static_cast<std::int32_t>(m_source);
    const auto* atlas = source();
    if (!atlas)
        return;
    std::int32_t chosen = -1;
    for (std::size_t i = 0; i < atlas->regions.size(); ++i)
    {
        if (atlas->regions[i].tile == tile)
            chosen = static_cast<std::int32_t>(i);
    }
    if (rebuilt || previousSource != m_source)
    {
        std::vector<std::string> labels;
        for (const auto& region : atlas->regions)
            labels.push_back(tileName(state, region));
        ui.setChoice(m_tileChoice, std::move(labels), chosen);
    }
    else
        ui.scene().get<scene::UiDropdown>(m_tileChoice).selected = chosen;
    const bool image = atlas->texture.isValid();
    ui.scene().get<scene::UiRect>(m_sheet).visible = image;
    ui.scene().get<scene::UiImage>(m_image).texture = atlas->texture;
    ui.showLine(section, m_toolbar, image);
    ui.showLine(section, m_pane, image);
    ui.showLine(section, m_sourceRow, m_sources.size() > 1);
    const float width = std::max(ui.panel.size().x - ui.font * 0.6f - 8 - ui.pad * 2, 48.0f);
    const float height = image ? std::clamp(width * 0.85f, ui.font * 12, ui.font * 24) : ui.line;
    auto& pane = ui.scene().get<scene::UiRect>(m_pane);
    pane.offsetMax.y = pane.offsetMin.y + height;
    const auto* placed = ui.panel.world().canvases().empty()
                             ? nullptr
                             : ui.panel.world().canvases().front().layout.find(m_pane);
    m_view.resize({placed ? placed->size().x : width, height});
    ui.scene().get<scene::UiText>(m_percent).text = std::format("{:.0f}%", m_view.scale * 100);
    ui.scene().get<scene::UiRect>(m_grid.entity).style = m_showGrid ? "primary" : "button";
    place(ui, tile);
}

void TileAtlasPalette::place(FormUi& ui, std::uint32_t tile)
{
    const auto* atlas = source();
    if (!atlas || !atlas->texture.isValid())
        return;
    auto& sheet = ui.scene().get<scene::UiRect>(m_sheet);
    sheet.anchorMin = sheet.anchorMax = {0, 0};
    sheet.offsetMin = m_view.offset;
    sheet.offsetMax = sheet.offsetMin + atlas->pixels * m_view.scale;
    const auto accent = linearColor(themeColors().accent);
    for (const auto& [entity, id] :
         {std::pair{m_selected, tile}, std::pair{m_hover, m_hovered.value_or(0)}})
    {
        auto& rect = ui.scene().get<scene::UiRect>(entity);
        const auto found = std::ranges::find(atlas->regions, id, &TileAtlasRegion::tile);
        rect.visible = found != atlas->regions.end();
        if (rect.visible)
        {
            rect = regionRect(*found, atlas->pixels);
            auto& outline = ui.scene().get<scene::UiLine>(entity);
            const auto size = (found->max - found->min) * m_view.scale;
            outline.points = {{0, 0}, {size.x, 0}, size, {0, size.y}};
            outline.color =
                entity == m_selected ? accent : math::Vec4{accent.x, accent.y, accent.z, 0.55f};
        }
    }
    const auto parent = m_gridLines.front();
    std::size_t shown = 1;
    if (m_showGrid)
        for (const auto& region : atlas->regions)
        {
            const auto low = m_view.offset + region.min * m_view.scale;
            const auto high = m_view.offset + region.max * m_view.scale;
            if (high.x < 0 || high.y < 0 || low.x > m_view.viewport.x ||
                low.y > m_view.viewport.y || high.x - low.x < 8 || high.y - low.y < 8 ||
                shown > 1024)
                continue;
            if (shown == m_gridLines.size())
            {
                const auto made = ui.add(parent, "Tile Boundary", rects::whole());
                ui.scene().add<scene::UiLine>(made, scene::UiLine{.width = 1, .closed = true});
                m_gridLines.push_back(made);
            }
            const auto entity = m_gridLines[shown++];
            ui.scene().get<scene::UiRect>(entity) = regionRect(region, atlas->pixels);
            auto& outline = ui.scene().get<scene::UiLine>(entity);
            const auto size = (region.max - region.min) * m_view.scale;
            outline.points = {{0, 0}, {size.x, 0}, size, {0, size.y}};
            outline.color = math::Vec4{accent.x, accent.y, accent.z, 0.22f};
        }
    for (; shown < m_gridLines.size(); ++shown)
        ui.scene().get<scene::UiRect>(m_gridLines[shown]).visible = false;
}

std::optional<std::uint32_t> TileAtlasPalette::answer(FormUi& ui, ToolsState& state)
{
    const auto& world = ui.panel.world();
    const auto choose = [&](std::uint32_t tile)
    {
        state.animating = true;
        return std::optional{tile};
    };
    if (world.wasChanged(m_sourceChoice))
    {
        const auto index = ui.scene().get<scene::UiDropdown>(m_sourceChoice).selected;
        if (index >= 0 && static_cast<std::size_t>(index) < m_sources.size() &&
            !m_sources[static_cast<std::size_t>(index)].regions.empty())
            return choose(m_sources[static_cast<std::size_t>(index)].regions.front().tile);
    }
    if (world.wasChanged(m_tileChoice) && source())
    {
        const auto index = ui.scene().get<scene::UiDropdown>(m_tileChoice).selected;
        if (index >= 0 && static_cast<std::size_t>(index) < source()->regions.size())
            return choose(source()->regions[static_cast<std::size_t>(index)].tile);
    }
    const auto* pane =
        world.canvases().empty() ? nullptr : world.canvases().front().layout.find(m_pane);
    if (!pane || !pane->visible || !source() || !source()->texture.isValid())
    {
        m_panning.reset();
        m_hovered.reset();
        return std::nullopt;
    }
    const auto beforeOffset = m_view.offset;
    const auto beforeScale = m_view.scale;
    const auto beforeHover = m_hovered;
    const bool beforeGrid = m_showGrid;
    m_view.resize(pane->size());
    const auto center = pane->size() * 0.5f;
    if (world.wasClicked(m_zoomOut.entity))
        m_view.zoom(1 / 1.25f, center);
    if (world.wasClicked(m_zoomIn.entity))
        m_view.zoom(1.25f, center);
    if (world.wasClicked(m_fit.entity))
        m_view.fit();
    if (world.wasClicked(m_grid.entity))
        m_showGrid = !m_showGrid;
    const auto& input = ui.panel.input();
    const bool over = world.hovered() == m_pane;
    if (over && input.wheel != 0)
        m_view.zoom(std::pow(1.25f, input.wheel), input.pointer - pane->min);
    if (over)
        for (const auto button : {Mouse::Middle, Mouse::Right})
            if (state.input.clicked(button))
            {
                m_panning = button;
                m_lastPointer = ui.panel.unitsOf(state.input.mouse());
            }
    if (m_panning)
    {
        if (!state.input.down(*m_panning))
            m_panning.reset();
        else
        {
            const auto at = ui.panel.unitsOf(state.input.mouse());
            m_view.pan(at - m_lastPointer);
            m_lastPointer = at;
        }
    }
    m_hovered = over && !m_panning
                    ? std::optional{source()->tileAt(m_view.pixelAt(input.pointer - pane->min))}
                    : std::nullopt;
    ui.tooltip(m_pane,
               m_hovered.value_or(0) != 0
                   ? std::format("Tile {}. Click to paint; wheel to zoom.", *m_hovered)
                   : "No tile here. Wheel to zoom; middle or right drag to move the sheet.");
    // The next layout must be drawn even when the editor sleeps between input events.
    if (beforeOffset != m_view.offset || beforeScale != m_view.scale || beforeHover != m_hovered ||
        beforeGrid != m_showGrid)
        state.animating = true;
    if (over && input.pointerPressed && m_hovered.value_or(0) != 0)
        return choose(*m_hovered);
    return std::nullopt;
}

} // namespace devex::tools::detail
