// The page of a tileset in the inspector: its tiles as a palette, which sprites dropped from FileSystem
// join, the tile chosen: its sprite, its collision, its data and the frames that animate it, and the
// terrain sets, whose terrains are painted on the middle, the sides and the corners of the tiles of
// the palette, as Godot paints them. Saved once a change is over.
#include "InspectorUi.hpp"

#include <devex/asset/import/TilesetFile.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>

#include <algorithm>
#include <array>
#include <cfloat>
#include <format>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>

namespace devex::tools::detail {
namespace {

using scene::Entity;
using Button = PanelButton;

struct CollisionChoice
{
    asset::TileCollision collision;
    const char* label;
    const char* tooltip;
};

constexpr std::array collisionChoices{
    CollisionChoice{asset::TileCollision::None, "None", "Walked through: grass, decorations, backgrounds"},
    CollisionChoice{asset::TileCollision::Full, "Full", "Solid on every side: ground, walls"},
    CollisionChoice{asset::TileCollision::Top, "Top", "Holds up what lands on it, lets through what comes from below: ledges"},
};

struct ModeChoice
{
    asset::TerrainMode mode;
    const char* label;
};

constexpr std::array modeChoices{
    ModeChoice{asset::TerrainMode::CornersAndSides, "Match Corners and Sides"},
    ModeChoice{asset::TerrainMode::Corners, "Match Corners"},
    ModeChoice{asset::TerrainMode::Sides, "Match Sides"},
};

// The colours new terrains take, in turn.
constexpr std::array terrainColors{
    math::Vec4{0.18f, 0.55f, 0.10f, 1.0f}, math::Vec4{0.45f, 0.20f, 0.06f, 1.0f}, math::Vec4{0.05f, 0.25f, 0.80f, 1.0f},
    math::Vec4{0.80f, 0.60f, 0.05f, 1.0f}, math::Vec4{0.55f, 0.08f, 0.45f, 1.0f}, math::Vec4{0.08f, 0.55f, 0.55f, 1.0f},
};

// The nine parts of a tile in the palette, row by row from the top-left one: the side or corner each
// one paints, or its middle.
constexpr std::size_t middlePart = asset::tileNeighborCount;
constexpr std::array<std::size_t, 9> partNeighbors{3, 2, 1, 4, middlePart, 0, 5, 6, 7};

[[nodiscard]] std::filesystem::file_time_type writeTime(const std::filesystem::path& file)
{
    std::error_code error;
    const std::filesystem::file_time_type time = std::filesystem::last_write_time(file, error);
    return error ? std::filesystem::file_time_type{} : time;
}

// Reads the tileset of the selected asset from its file, when another asset is selected or the
// file changed outside the editor.
void loadTileset(ToolsState& state, const asset::SourceFile& source)
{
    TilesetEditor& editor = state.tilesetEditor;
    const std::optional<std::filesystem::path> file = state.database->project().absolutePath(source.path);
    if (!file)
    {
        return;
    }
    const std::filesystem::file_time_type time = writeTime(*file);
    if (editor.asset == state.selectedAsset && editor.file == *file && editor.fileTime == time)
    {
        return;
    }
    const bool sameAsset = editor.asset == state.selectedAsset;
    editor.asset = state.selectedAsset;
    editor.file = *file;
    editor.fileTime = time;
    editor.error.clear();
    const core::Result<std::string> text = core::readTextFile(*file);
    core::Result<asset::TilesetData> tileset =
        text ? asset::parseTilesetFile(*text) : core::Result<asset::TilesetData>(std::unexpected(text.error()));
    if (!tileset)
    {
        editor.error = tileset.error().message;
        editor.tileset = {};
    }
    else
    {
        editor.tileset = std::move(*tileset);
    }
    if (!sameAsset || editor.tileset.find(editor.selectedTile) == nullptr)
    {
        editor.selectedTile = editor.tileset.tiles.empty() ? 0 : editor.tileset.tiles.front().id;
    }
    if (!sameAsset || editor.tileset.terrain(editor.paintSet, editor.paintTerrain) == nullptr)
    {
        editor.paintSet = asset::noTerrain;
        editor.paintTerrain = asset::noTerrain;
    }
}

void saveTileset(ToolsState& state)
{
    TilesetEditor& editor = state.tilesetEditor;
    if (core::Result<void> valid = asset::validate(editor.tileset); !valid)
    {
        DEVEX_LOG_WARNING("The tileset is not saved: {}", valid.error());
        return;
    }
    if (core::Result<void> written = core::writeTextFile(editor.file, asset::writeTilesetFile(editor.tileset)); !written)
    {
        DEVEX_LOG_ERROR("Cannot save the tileset: {}", written.error());
        return;
    }
    editor.fileTime = writeTime(editor.file);
    editor.error.clear();
    if (core::Result<void> queued = state.database->reimport(editor.asset); !queued)
    {
        DEVEX_LOG_WARNING("{}", queued.error());
    }
}

// Adds a tile for each sprite the tileset does not show yet; returns the first one added.
std::uint32_t addTiles(asset::TilesetData& tileset, const std::vector<asset::AssetId>& sprites)
{
    std::uint32_t first = 0;
    for (const asset::AssetId sprite : sprites)
    {
        if (std::ranges::any_of(tileset.tiles, [&](const asset::TileData& tile) { return tile.sprite == sprite; }))
        {
            continue;
        }
        const std::uint32_t id = tileset.nextId();
        if (id > asset::maxTileId)
        {
            break;
        }
        tileset.tiles.push_back({.id = id, .sprite = sprite});
        first = first == 0 ? id : first;
    }
    return first;
}

// A list of sprites to choose from, with nothing first: all of them while it may open, the chosen one
// otherwise, and the sprites it stands for.
void fillSpriteChoice(InspectorUi& ui, const ToolsState& state, Entity list, asset::AssetId value, std::vector<asset::AssetId>& sprites)
{
    const ui::UiWorld& world = ui.panel.world();
    const bool full = world.hovered() == list || world.focused() == list || world.listedDropdown() == list;
    sprites.assign(1, asset::AssetId{});
    if (full && state.database != nullptr)
    {
        for (const asset::AssetInfo& info : state.database->assets(asset::AssetType::Sprite))
        {
            sprites.push_back(info.id);
        }
    }
    else if (value.isValid())
    {
        sprites.push_back(value);
    }
    std::vector<std::string> options;
    std::int32_t selected = -1;
    for (const asset::AssetId sprite : sprites)
    {
        if (sprite == value)
        {
            selected = static_cast<std::int32_t>(options.size());
        }
        options.push_back(assetLabel(state, sprite));
    }
    scene::UiDropdown& dropdown = ui.scene().get<scene::UiDropdown>(list);
    if (dropdown.options != options)
    {
        dropdown.options = std::move(options);
    }
    dropdown.selected = selected;
    dropdown.placeholder = assetLabel(state, value);
}

[[nodiscard]] std::string terrainLabel(const asset::TilesetData& tileset, std::int32_t set, std::int32_t terrain)
{
    const asset::TerrainData* const data = tileset.terrain(set, terrain);
    if (data == nullptr)
    {
        return "None";
    }
    return std::format("{} (set {})", data->name.empty() ? std::string("unnamed") : data->name, set);
}

[[nodiscard]] std::string tileTooltip(const ToolsState& state, const asset::TilesetData& tileset, const asset::TileData& tile)
{
    const asset::AssetInfo* const info = state.database != nullptr ? state.database->find(tile.sprite) : nullptr;
    return std::format("Tile {}: {}\nCollision: {}{}{}{}", tile.id, info != nullptr ? info->name : std::string("(no sprite)"),
                       asset::toString(tile.collision), tile.frames.empty() ? "" : "\nAnimated",
                       tile.data.empty() ? std::string{} : std::format("\nData: {}", tile.data),
                       tile.terrainSet == asset::noTerrain ? std::string{}
                                                           : std::format("\nTerrain: {}", terrainLabel(tileset, tile.terrainSet, tile.terrain)));
}

// Paints a part of a tile with the terrain of the brush, or clears it: a tile of another set joins
// the set of the brush, as it was without terrains, and a tile left without any leaves its set.
[[nodiscard]] bool paintPart(asset::TileData& tile, std::size_t neighbor, std::int32_t set, std::int32_t terrain, bool clear)
{
    const asset::TileData before = tile;
    if (!clear && tile.terrainSet != set)
    {
        tile.terrainSet = set;
        tile.terrain = asset::noTerrain;
        tile.terrainBits = asset::noTerrainBits;
    }
    if (tile.terrainSet == asset::noTerrain)
    {
        return false;
    }
    std::int32_t& part = neighbor == middlePart ? tile.terrain : tile.terrainBits[neighbor];
    part = clear ? asset::noTerrain : terrain;
    if (tile.terrain == asset::noTerrain && tile.terrainBits == asset::noTerrainBits)
    {
        tile.terrainSet = asset::noTerrain;
    }
    return tile != before;
}

class TilesetPage final : public InspectorPage
{
public:
    std::string signature(ToolsState& state) override
    {
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        if (!source)
        {
            return {};
        }
        loadTileset(state, *source);
        const TilesetEditor& editor = state.tilesetEditor;
        const asset::TileData* const tile = editor.tileset.find(editor.selectedTile);
        std::string terrains;
        for (const asset::TerrainSetData& set : editor.tileset.terrainSets)
        {
            terrains += std::format("{},", set.terrains.size());
        }
        return std::format("{}|{}|{}|{}|{}|{}", editor.tileset.tiles.size(), tile != nullptr ? tile->id : 0u,
                           tile != nullptr ? tile->frames.size() : 0u, editor.error.empty(), terrains,
                           editor.paintSet != asset::noTerrain);
    }

    void build(InspectorUi& ui, ToolsState& state, EditorUiKit& kit) override
    {
        const ThemeColors& colors = themeColors();
        TilesetEditor& editor = state.tilesetEditor;
        m_tiles.reset();
        m_frames.reset();
        m_sprite = {};
        m_zones.clear();
        m_sets.clear();
        const asset::AssetInfo* const info = state.database->find(state.selectedAsset);
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        if (info == nullptr || !source)
        {
            return;
        }
        ui.heading(kit, icons::Grid, colors.texture, info->name, source->path);
        if (!editor.error.empty())
        {
            ui.note(nullptr, std::format("The file could not be read: {}", editor.error), "error", 2.0f);
            ui.note(nullptr, "Editing the tileset writes a new one over it.");
        }

        ui.card(kit, "Tiles");
        m_tilesCard = ui.sections.size() - 1;
        Section& tiles = ui.sections[m_tilesCard];
        const FormRow paintRow = ui.formRow(tiles, "Paint Terrain");
        m_paint = ui.choice(paintRow.editor);
        ui.tooltip(paintRow.editor, "Choose a terrain to paint it on the middle, the sides and the corners of the tiles below");
        m_paintHint = ui.note(&tiles, "", "dim", 2.0f);
        ui.spriteGrid(kit, tiles, m_tiles, editor.tileset.tiles.size(), std::round(ui.font * 3.2f), true);
        ui.tooltip(m_tiles->drop, "Drop sprites from the FileSystem here, or a texture to add a tile for each of its sprites");
        if (editor.paintSet != asset::noTerrain)
        {
            // The parts of each tile take the brush; the clicks reach them rather than the tile.
            for (const Entity cell : m_tiles->cells)
            {
                std::array<Entity, 9>& parts = m_zones.emplace_back();
                const float inset = std::round(m_tiles->size * 0.06f);
                for (std::size_t index = 0; index < parts.size(); ++index)
                {
                    const float column = static_cast<float>(index % 3);
                    const float row = static_cast<float>(index / 3);
                    parts[index] = ui.add(cell, "Part",
                                          scene::UiRect{.anchorMin = {column / 3.0f, row / 3.0f},
                                                        .anchorMax = {(column + 1.0f) / 3.0f, (row + 1.0f) / 3.0f},
                                                        .offsetMin = {index % 3 == 0 ? inset : 1.0f, index / 3 == 0 ? inset : 1.0f},
                                                        .offsetMax = {index % 3 == 2 ? -inset : -1.0f, index / 3 == 2 ? -inset : -1.0f}});
                    ui.scene().add<scene::UiImage>(parts[index], scene::UiImage{.color = math::Vec4{0.0f}, .cornerRadius = 2.0f});
                    ui.scene().add<scene::UiButton>(parts[index]);
                }
            }
        }

        const asset::TileData* const tile = editor.tileset.find(editor.selectedTile);
        if (tile != nullptr)
        {
            ui.card(kit, "Tile", EntityIcon{icons::Grid, colors.texture});
            m_tileCard = ui.sections.size() - 1;
            Section& card = ui.sections[m_tileCard];
            const FormRow spriteRow = ui.formRow(card, "Sprite");
            m_sprite = ui.choice(spriteRow.editor);
            const math::Vec4 accent = linearColor(colors.accent);
            ui.scene().add<scene::UiDropTarget>(m_sprite, scene::UiDropTarget{.accepts = {"asset:sprite"},
                                                                              .highlightColor = math::Vec4{accent.x, accent.y, accent.z, 0.35f}});
            const FormRow collisionRow = ui.formRow(card, "Collision");
            std::vector<std::string> labels;
            for (const CollisionChoice& choice : collisionChoices)
            {
                labels.emplace_back(choice.label);
            }
            m_collision = ui.choice(collisionRow.editor, std::move(labels));
            const FormRow dataRow = ui.formRow(card, "Data");
            m_data = ui.textField(dataRow.editor, "water, damage=5...");
            ui.tooltip(dataRow.editor, "Anything the game reads of the tile with Tilemaps.GetData");
            const FormRow terrainRow = ui.formRow(card, "Terrain");
            m_terrainText = ui.text(terrainRow.editor, rects::whole(), "", "dim");
            ui.tooltip(terrainRow.editor, "The terrain of the middle of the tile, painted with Paint Terrain above");
            const FormRow probabilityRow = ui.formRow(card, "Probability");
            const std::array<std::string_view, 1> one{""};
            m_probability =
                ui.numbers(probabilityRow.editor, one, {.minValue = 0.0f, .maxValue = 1000.0f, .dragSpeed = 0.01f, .decimals = 2}).front();
            ui.tooltip(probabilityRow.editor, "How often terrain brushes choose this tile among those that fit the same place: variants");
            const FormRow fpsRow = ui.formRow(card, "Frames per second");
            m_fps = ui.numbers(fpsRow.editor, one, {.minValue = 0.0f, .maxValue = 120.0f, .dragSpeed = 0.1f, .decimals = 2}).front();
            ui.tooltip(fpsRow.editor, "How fast an animated tile goes through its frames");
            // The frames of an animated tile, which replace its sprite while it plays.
            m_animation = ui.note(&card, "");
            ui.spriteGrid(kit, card, m_frames, tile->frames.size(), std::round(ui.font * 2.6f), true);
            ui.tooltip(m_frames->drop, "Drop sprites here to animate the tile: water, lava, torches");
            m_frameMenu = ui.pageMenu("Tile frame menu", ui.font * 10.0f);
            m_removeFrame = ui.menuItem(kit, m_frameMenu, Icon::Trash, "Remove");
            for (const Entity cell : m_frames->cells)
            {
                ui.scene().get<scene::UiContextMenu>(cell).popup = ui.scene().reference(m_frameMenu);
            }
            const Entity actions = ui.actions(&card);
            m_removeTile = ui.action(kit, actions, Icon::Trash, "Remove Tile");
            ui.tooltip(m_removeTile.entity, "Cells painted with it show nothing until a tile takes its number again");
        }

        buildTerrains(ui, state, kit);
        ui.note(nullptr, "A Tilemap that names this tileset paints its cells with these tiles: select it, then choose a tool under it in the "
                         "inspector.",
                "dim", 2.0f);
    }

    void sync(InspectorUi& ui, ToolsState& state, EditorUiKit&) override
    {
        TilesetEditor& editor = state.tilesetEditor;
        const asset::TilesetData& tileset = editor.tileset;
        if (m_tiles == nullptr || m_tiles->cells.size() != tileset.tiles.size())
        {
            return;
        }
        syncTerrains(ui, editor);
        // Animated tiles play in the palette as in the game.
        const double seconds = state.input.time();
        for (std::size_t index = 0; index < tileset.tiles.size(); ++index)
        {
            const asset::TileData& tile = tileset.tiles[index];
            ui.showSprite(m_tiles->images[index], tile.spriteAt(seconds));
            ui.scene().get<scene::UiRect>(m_tiles->cells[index]).style = tile.id == editor.selectedTile ? "row_selected" : "row";
            ui.tooltip(m_tiles->cells[index], tileTooltip(state, tileset, tile));
            if (index < m_zones.size())
            {
                syncParts(ui, tileset, tile, m_zones[index], editor.paintSet);
            }
        }
        ui.scene().get<scene::UiText>(ui.sections[m_tilesCard].title).text = std::format("Tiles ({})", tileset.tiles.size());
        const asset::TileData* const tile = tileset.find(editor.selectedTile);
        if (tile == nullptr || !m_sprite.isValid() || m_frames == nullptr || m_frames->cells.size() != tile->frames.size())
        {
            return;
        }
        const ui::UiWorld& world = ui.panel.world();
        ui.scene().get<scene::UiText>(ui.sections[m_tileCard].title).text = std::format("Tile {}", tile->id);
        fillSpriteChoice(ui, state, m_sprite, tile->sprite, m_sprites);
        const auto chosen = std::ranges::find(collisionChoices, tile->collision, &CollisionChoice::collision);
        ui.scene().get<scene::UiDropdown>(m_collision).selected =
            chosen != collisionChoices.end() ? static_cast<std::int32_t>(chosen - collisionChoices.begin()) : -1;
        ui.tooltip(m_collision, chosen != collisionChoices.end() ? std::string(chosen->tooltip) : std::string{});
        if (world.editedField() != m_data)
        {
            ui.scene().get<scene::UiText>(m_data).text = tile->data;
        }
        ui.scene().get<scene::UiText>(m_terrainText).text =
            tile->terrainSet == asset::noTerrain ? std::string("None") : terrainLabel(tileset, tile->terrainSet, tile->terrain);
        ui.setNumber(m_probability, tile->probability);
        if (world.editedField() != m_fps && world.held() != m_fps)
        {
            ui.scene().get<scene::UiNumberField>(m_fps).value = tile->fps;
        }
        ui.scene().get<scene::UiText>(m_animation).text =
            std::format("Animation: {} {}", tile->frames.size(), tile->frames.size() == 1 ? "frame" : "frames");
        for (std::size_t index = 0; index < tile->frames.size(); ++index)
        {
            ui.showSprite(m_frames->images[index], tile->frames[index]);
            ui.tooltip(m_frames->cells[index], std::format("Frame {}: right-click to remove it", index + 1));
        }
        ui.fitMenu(m_frameMenu, ui.font * 10.0f);
    }

    void answer(InspectorUi& ui, ToolsState& state, EditorUiKit&) override
    {
        TilesetEditor& editor = state.tilesetEditor;
        asset::TilesetData& tileset = editor.tileset;
        if (m_tiles == nullptr || m_tiles->cells.size() != tileset.tiles.size())
        {
            return;
        }
        const ui::UiWorld& world = ui.panel.world();
        answerTerrains(ui, editor);
        std::vector<asset::AssetId> dropped;
        for (std::size_t index = 0; index < tileset.tiles.size(); ++index)
        {
            if (world.wasClicked(m_tiles->cells[index]))
            {
                editor.selectedTile = tileset.tiles[index].id;
            }
            if (world.wasDropped(m_tiles->cells[index]) && world.dropped() != nullptr)
            {
                dropped = droppedSprites(state, *world.dropped());
            }
        }
        if (world.wasDropped(m_tiles->drop) && world.dropped() != nullptr)
        {
            dropped = droppedSprites(state, *world.dropped());
        }
        if (!dropped.empty())
        {
            if (const std::uint32_t added = addTiles(tileset, dropped); added != 0)
            {
                editor.selectedTile = added;
                m_dirty = true;
            }
        }
        answerParts(ui, editor);

        asset::TileData* const tile = [&]() -> asset::TileData* {
            const auto found = std::ranges::find(tileset.tiles, editor.selectedTile, &asset::TileData::id);
            return found != tileset.tiles.end() ? &*found : nullptr;
        }();
        if (tile != nullptr && m_sprite.isValid() && m_frames != nullptr && m_frames->cells.size() == tile->frames.size())
        {
            answerTile(ui, state, *tile);
        }
        const bool busy = world.held().isValid() || world.isEditing() || m_painting;
        if (m_dirty && !busy)
        {
            saveTileset(state);
            m_dirty = false;
        }
    }

private:
    // One terrain of a set: its name, its colour, and the button that removes it.
    struct TerrainRow
    {
        Entity name;
        Entity swatch;
        Button remove;
    };

    struct SetRows
    {
        Entity mode;
        Entity mirrorX;
        Entity mirrorY;
        std::vector<TerrainRow> terrains;
        Button addTerrain;
        Button remove;
    };

    void buildTerrains(InspectorUi& ui, const ToolsState& state, EditorUiKit& kit)
    {
        const asset::TilesetData& tileset = state.tilesetEditor.tileset;
        ui.card(kit, "Terrains");
        Section& card = ui.sections.back();
        if (tileset.terrainSets.empty())
        {
            ui.note(&card, "Terrains choose the tile of each cell by the cells around it: grass with its edges and corners, roads, water. "
                           "Add a terrain set, name its terrains, then paint them on the tiles.",
                    "dim", 3.0f);
        }
        std::vector<std::string> modes;
        for (const ModeChoice& choice : modeChoices)
        {
            modes.emplace_back(choice.label);
        }
        for (std::size_t set = 0; set < tileset.terrainSets.size(); ++set)
        {
            SetRows& rows = m_sets.emplace_back();
            const FormRow modeRow = ui.formRow(card, std::format("Terrain Set {}", set));
            rows.mode = ui.choice(modeRow.editor, modes);
            ui.tooltip(modeRow.editor, "Which neighbours the tiles of the set match: sides and corners (47 tiles for a whole terrain), "
                                       "corners only or sides only (16 tiles)");
            const FormRow mirrorXRow = ui.formRow(card, "Flip X");
            rows.mirrorX = ui.toggle(mirrorXRow.editor);
            ui.tooltip(mirrorXRow.editor, "Brushes may mirror the tiles from left to right: a left edge also serves as a right edge");
            const FormRow mirrorYRow = ui.formRow(card, "Flip Y");
            rows.mirrorY = ui.toggle(mirrorYRow.editor);
            ui.tooltip(mirrorYRow.editor, "Brushes may mirror the tiles from top to bottom");
            for (std::size_t terrain = 0; terrain < tileset.terrainSets[set].terrains.size(); ++terrain)
            {
                TerrainRow& row = rows.terrains.emplace_back();
                const FormRow terrainRow = ui.formRow(card, std::format("Terrain {}", terrain));
                const Entity nameBox = ui.add(terrainRow.editor, "Name",
                                              scene::UiRect{.anchorMin = {0.0f, 0.0f},
                                                            .anchorMax = {0.45f, 1.0f},
                                                            .offsetMin = {0.0f, 0.0f},
                                                            .offsetMax = {0.0f, 0.0f}});
                row.name = ui.textField(nameBox, "Grass, water...");
                const Entity colorBox = ui.add(terrainRow.editor, "Color",
                                               scene::UiRect{.anchorMin = {0.47f, 0.0f},
                                                             .anchorMax = {1.0f, 1.0f},
                                                             .offsetMin = {0.0f, 0.0f},
                                                             .offsetMax = {-ui.line - ui.font * 0.2f, 0.0f}});
                row.swatch = ui.swatch(kit, colorBox, false);
                row.remove = ui.toolButton(kit, terrainRow.editor, Icon::Trash,
                                           scene::UiRect{.anchorMin = {1.0f, 0.5f},
                                                         .anchorMax = {1.0f, 0.5f},
                                                         .offsetMin = {-ui.line + 2.0f, -ui.line * 0.5f + 2.0f},
                                                         .offsetMax = {0.0f, ui.line * 0.5f - 2.0f}});
                ui.tooltip(row.remove.entity, "Remove the terrain: the tiles that name it name none there");
            }
            const Entity actions = ui.actions(&card);
            rows.addTerrain = ui.action(kit, actions, Icon::Plus, "Add Terrain");
            rows.remove = ui.action(kit, actions, Icon::Trash, "Remove Set");
            ui.tooltip(rows.remove.entity, "Remove the terrain set: its tiles stay, without terrains");
        }
        const Entity actions = ui.actions(&card);
        m_addSet = ui.action(kit, actions, Icon::Plus, "Add Terrain Set");
    }

    void syncTerrains(InspectorUi& ui, TilesetEditor& editor)
    {
        const asset::TilesetData& tileset = editor.tileset;
        if (tileset.terrain(editor.paintSet, editor.paintTerrain) == nullptr)
        {
            editor.paintSet = asset::noTerrain;
            editor.paintTerrain = asset::noTerrain;
        }
        // The brush: nothing, which selects tiles, or a terrain.
        std::vector<std::string> options{"None: click selects a tile"};
        m_paintChoices.assign(1, {asset::noTerrain, asset::noTerrain});
        std::int32_t selected = 0;
        for (std::size_t set = 0; set < tileset.terrainSets.size(); ++set)
        {
            for (std::size_t terrain = 0; terrain < tileset.terrainSets[set].terrains.size(); ++terrain)
            {
                const auto ids = std::pair{static_cast<std::int32_t>(set), static_cast<std::int32_t>(terrain)};
                if (ids == std::pair{editor.paintSet, editor.paintTerrain})
                {
                    selected = static_cast<std::int32_t>(options.size());
                }
                options.push_back(terrainLabel(tileset, ids.first, ids.second));
                m_paintChoices.push_back(ids);
            }
        }
        ui.setChoice(m_paint, std::move(options), selected);
        ui.scene().get<scene::UiText>(m_paintHint).text =
            tileset.terrainSets.empty() ? "Add a terrain set below to paint terrains on the tiles."
            : editor.paintSet == asset::noTerrain
                ? "Choose a terrain to paint it on the tiles."
                : "Click or drag on the middle, the sides and the corners of the tiles to paint the terrain there; right-click clears.";
        for (std::size_t set = 0; set < m_sets.size() && set < tileset.terrainSets.size(); ++set)
        {
            const asset::TerrainSetData& data = tileset.terrainSets[set];
            SetRows& rows = m_sets[set];
            const auto mode = std::ranges::find(modeChoices, data.mode, &ModeChoice::mode);
            ui.scene().get<scene::UiDropdown>(rows.mode).selected = static_cast<std::int32_t>(mode - modeChoices.begin());
            ui.setToggle(rows.mirrorX, data.mirrorX);
            ui.setToggle(rows.mirrorY, data.mirrorY);
            for (std::size_t terrain = 0; terrain < rows.terrains.size() && terrain < data.terrains.size(); ++terrain)
            {
                ui.setText(rows.terrains[terrain].name, data.terrains[terrain].name);
                ui.setSwatch(rows.terrains[terrain].swatch, data.terrains[terrain].color, false);
            }
        }
        if (m_colorTerrain && ui.colorPopupOpen())
        {
            if (const asset::TerrainData* const terrain = tileset.terrain(m_colorTerrain->first, m_colorTerrain->second))
            {
                ui.syncColorPopup(terrain->color, false);
            }
        }
    }

    // The parts of a tile in the palette: those its set reads, or the set of the brush for a tile
    // without terrains, in the colour of the terrain each names.
    static void syncParts(InspectorUi& ui, const asset::TilesetData& tileset, const asset::TileData& tile, const std::array<Entity, 9>& parts,
                          std::int32_t brushSet)
    {
        const std::int32_t set = tile.terrainSet != asset::noTerrain ? tile.terrainSet : brushSet;
        const asset::TerrainSetData* const data = tileset.terrainSet(set);
        for (std::size_t index = 0; index < parts.size(); ++index)
        {
            const std::size_t neighbor = partNeighbors[index];
            const bool shown = data != nullptr && (neighbor == middlePart ||
                                                   asset::matchesNeighbor(data->mode, static_cast<asset::TileNeighbor>(neighbor)));
            ui.scene().get<scene::UiRect>(parts[index]).visible = shown;
            const std::int32_t terrain = tile.terrainSet != set ? asset::noTerrain
                                         : neighbor == middlePart ? tile.terrain
                                                                  : tile.terrainBits[neighbor];
            const asset::TerrainData* const named = tileset.terrain(set, terrain);
            ui.scene().get<scene::UiImage>(parts[index]).color = named != nullptr
                                                                     ? math::Vec4{named->color.x, named->color.y, named->color.z, 0.7f}
                                                                     : math::Vec4{1.0f, 1.0f, 1.0f, 0.06f};
        }
    }

    // Painting the parts of the tiles: pressed on one, dragged over others; the right button clears.
    void answerParts(InspectorUi& ui, TilesetEditor& editor)
    {
        const ui::UiWorld& world = ui.panel.world();
        const ui::UiInput& input = ui.panel.input();
        if (!input.pointerDown)
        {
            m_painting = false;
        }
        if (editor.paintSet == asset::noTerrain || m_zones.size() != editor.tileset.tiles.size())
        {
            return;
        }
        const Entity hovered = world.hovered();
        for (std::size_t tile = 0; tile < m_zones.size(); ++tile)
        {
            const auto part = std::ranges::find(m_zones[tile], hovered);
            if (!hovered.isValid() || part == m_zones[tile].end())
            {
                continue;
            }
            const std::size_t neighbor = partNeighbors[static_cast<std::size_t>(part - m_zones[tile].begin())];
            asset::TileData& data = editor.tileset.tiles[tile];
            if (input.pointerPressed)
            {
                m_painting = true;
            }
            if (input.secondaryPressed)
            {
                m_dirty = paintPart(data, neighbor, editor.paintSet, editor.paintTerrain, true) || m_dirty;
            }
            else if (m_painting && input.pointerDown)
            {
                m_dirty = paintPart(data, neighbor, editor.paintSet, editor.paintTerrain, false) || m_dirty;
            }
        }
    }

    void answerTerrains(InspectorUi& ui, TilesetEditor& editor)
    {
        asset::TilesetData& tileset = editor.tileset;
        const ui::UiWorld& world = ui.panel.world();
        if (world.wasChanged(m_paint))
        {
            const std::int32_t index = ui.scene().get<scene::UiDropdown>(m_paint).selected;
            if (index >= 0 && static_cast<std::size_t>(index) < m_paintChoices.size())
            {
                std::tie(editor.paintSet, editor.paintTerrain) = m_paintChoices[static_cast<std::size_t>(index)];
            }
        }
        if (world.wasClicked(m_addSet.entity) && tileset.terrainSets.size() < asset::maxTerrainSets)
        {
            tileset.terrainSets.push_back({.terrains = {{.name = "Terrain 0", .color = terrainColors[0]}}});
            m_dirty = true;
        }
        for (std::size_t set = 0; set < m_sets.size() && set < tileset.terrainSets.size(); ++set)
        {
            SetRows& rows = m_sets[set];
            asset::TerrainSetData& data = tileset.terrainSets[set];
            if (world.wasChanged(rows.mode))
            {
                const std::int32_t index = ui.scene().get<scene::UiDropdown>(rows.mode).selected;
                if (index >= 0 && static_cast<std::size_t>(index) < modeChoices.size())
                {
                    data.mode = modeChoices[static_cast<std::size_t>(index)].mode;
                    for (asset::TileData& tile : tileset.tiles)
                    {
                        if (tile.terrainSet == static_cast<std::int32_t>(set))
                        {
                            asset::normalizeTerrains(tile, tileset);
                        }
                    }
                    m_dirty = true;
                }
            }
            if (world.wasChanged(rows.mirrorX))
            {
                data.mirrorX = ui.scene().get<scene::UiToggle>(rows.mirrorX).value;
                m_dirty = true;
            }
            if (world.wasChanged(rows.mirrorY))
            {
                data.mirrorY = ui.scene().get<scene::UiToggle>(rows.mirrorY).value;
                m_dirty = true;
            }
            for (std::size_t terrain = 0; terrain < rows.terrains.size() && terrain < data.terrains.size(); ++terrain)
            {
                TerrainRow& row = rows.terrains[terrain];
                const auto ids = std::pair{static_cast<std::int32_t>(set), static_cast<std::int32_t>(terrain)};
                if (world.editedField() == row.name)
                {
                    m_typingName = ids;
                }
                else if (m_typingName == ids)
                {
                    m_typingName.reset();
                    const std::string typed = ui.scene().get<scene::UiText>(row.name).text;
                    if (!ui.panel.input().cancelPressed && typed != data.terrains[terrain].name)
                    {
                        data.terrains[terrain].name = typed;
                        m_dirty = true;
                    }
                }
                if (world.wasClicked(row.swatch))
                {
                    m_colorTerrain = ids;
                    ui.openColorPopup(row.swatch);
                }
                if (world.wasClicked(row.remove.entity))
                {
                    tileset.removeTerrain(ids.first, ids.second);
                    m_dirty = true;
                    return;
                }
            }
            if (world.wasClicked(rows.addTerrain.entity) && data.terrains.size() < asset::maxTerrains)
            {
                const std::size_t count = data.terrains.size();
                data.terrains.push_back({.name = std::format("Terrain {}", count), .color = terrainColors[count % terrainColors.size()]});
                m_dirty = true;
            }
            if (world.wasClicked(rows.remove.entity))
            {
                tileset.removeTerrainSet(static_cast<std::int32_t>(set));
                m_dirty = true;
                return;
            }
        }
        if (m_colorTerrain)
        {
            const auto [set, terrain] = *m_colorTerrain;
            if (tileset.terrain(set, terrain) != nullptr)
            {
                asset::TerrainData& data = tileset.terrainSets[static_cast<std::size_t>(set)].terrains[static_cast<std::size_t>(terrain)];
                if (const std::optional<math::Vec4> chosen = ui.answerColorPopup(data.color))
                {
                    data.color = math::Vec4{chosen->x, chosen->y, chosen->z, 1.0f};
                    m_dirty = true;
                }
            }
            if (!ui.colorPopupOpen())
            {
                m_colorTerrain.reset();
            }
        }
    }

    void answerTile(InspectorUi& ui, ToolsState& state, asset::TileData& tile)
    {
        TilesetEditor& editor = state.tilesetEditor;
        const ui::UiWorld& world = ui.panel.world();
        if (world.wasChanged(m_sprite))
        {
            const std::int32_t selected = ui.scene().get<scene::UiDropdown>(m_sprite).selected;
            if (selected >= 0 && static_cast<std::size_t>(selected) < m_sprites.size())
            {
                tile.sprite = m_sprites[static_cast<std::size_t>(selected)];
                m_dirty = true;
            }
        }
        if (world.wasDropped(m_sprite) && world.dropped() != nullptr)
        {
            if (const std::optional<core::Uuid> uuid = core::Uuid::parse(world.dropped()->data))
            {
                tile.sprite = asset::AssetId{*uuid};
                m_dirty = true;
            }
        }
        if (world.wasChanged(m_collision))
        {
            const std::int32_t selected = ui.scene().get<scene::UiDropdown>(m_collision).selected;
            if (selected >= 0 && static_cast<std::size_t>(selected) < collisionChoices.size())
            {
                tile.collision = collisionChoices[static_cast<std::size_t>(selected)].collision;
                m_dirty = true;
            }
        }
        if (world.editedField() == m_data)
        {
            m_typing = true;
        }
        else if (std::exchange(m_typing, false) && !ui.panel.input().cancelPressed)
        {
            const std::string typed = ui.scene().get<scene::UiText>(m_data).text;
            if (typed != tile.data)
            {
                tile.data = typed;
                m_dirty = true;
            }
        }
        if (world.wasChanged(m_probability))
        {
            tile.probability = std::clamp(ui.scene().get<scene::UiNumberField>(m_probability).value, 0.0f, 1000.0f);
            m_dirty = true;
        }
        if (world.wasChanged(m_fps))
        {
            tile.fps = std::clamp(ui.scene().get<scene::UiNumberField>(m_fps).value, 0.0f, 120.0f);
            m_dirty = true;
        }
        if (world.wasDropped(m_frames->drop) && world.dropped() != nullptr)
        {
            const std::vector<asset::AssetId> frames = droppedSprites(state, *world.dropped());
            if (!frames.empty())
            {
                tile.frames.insert(tile.frames.end(), frames.begin(), frames.end());
                if (tile.fps <= 0.0f)
                {
                    tile.fps = 6.0f;
                }
                m_dirty = true;
            }
        }
        if (ui.panel.input().secondaryPressed && world.isPopupOpen(ui.scene(), m_frameMenu))
        {
            const auto found = std::ranges::find(m_frames->cells, world.contextTarget());
            m_menuFrame = found != m_frames->cells.end() ? std::optional(static_cast<std::size_t>(found - m_frames->cells.begin()))
                                                         : std::nullopt;
        }
        if (world.wasClicked(m_removeFrame.entity) && m_menuFrame && *m_menuFrame < tile.frames.size())
        {
            tile.frames.erase(tile.frames.begin() + static_cast<std::ptrdiff_t>(*m_menuFrame));
            m_dirty = true;
        }
        if (!world.isPopupOpen(ui.scene(), m_frameMenu))
        {
            m_menuFrame.reset();
        }
        if (world.wasClicked(m_removeTile.entity))
        {
            const std::uint32_t id = tile.id;
            std::erase_if(editor.tileset.tiles, [id](const asset::TileData& other) { return other.id == id; });
            editor.selectedTile = editor.tileset.tiles.empty() ? 0 : editor.tileset.tiles.front().id;
            m_dirty = true;
        }
    }

    std::unique_ptr<SpriteGrid> m_tiles;
    std::size_t m_tilesCard = 0;
    std::size_t m_tileCard = 0;
    Entity m_sprite;
    std::vector<asset::AssetId> m_sprites;
    Entity m_collision;
    Entity m_data;
    bool m_typing = false;
    Entity m_terrainText;
    Entity m_probability;
    Entity m_fps;
    Entity m_animation;
    std::unique_ptr<SpriteGrid> m_frames;
    Entity m_frameMenu;
    Button m_removeFrame;
    std::optional<std::size_t> m_menuFrame;
    Button m_removeTile;
    // The terrain painted on the tiles, the parts of each tile, and whether a stroke goes on.
    Entity m_paint;
    Entity m_paintHint;
    std::vector<std::pair<std::int32_t, std::int32_t>> m_paintChoices;
    std::vector<std::array<Entity, 9>> m_zones;
    bool m_painting = false;
    std::vector<SetRows> m_sets;
    Button m_addSet;
    std::optional<std::pair<std::int32_t, std::int32_t>> m_typingName;
    std::optional<std::pair<std::int32_t, std::int32_t>> m_colorTerrain;
    bool m_dirty = false;
};

} // namespace

std::unique_ptr<InspectorPage> makeTilesetPage()
{
    return std::make_unique<TilesetPage>();
}

core::Result<std::filesystem::path> createTilesetFile(ToolsState& state, std::string_view folder,
                                                    asset::AssetId fromTexture, std::string_view name)
{
    const asset::AssetInfo* const texture = state.database && fromTexture.isValid() ? state.database->find(fromTexture) : nullptr;
    const std::string baseName = texture != nullptr ? std::format("{} Tiles", texture->name) : std::string("Tileset");
    asset::TilesetData tileset;
    if (texture != nullptr)
    {
        static_cast<void>(addTiles(tileset, spritesOfTexture(state, fromTexture)));
    }
    return writeNewAssetFile(state, folder, name, baseName, asset::tilesetExtension, asset::writeTilesetFile(tileset));
}

} // namespace devex::tools::detail
