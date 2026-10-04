// The page of a tileset in the inspector: its tiles as a palette, which sprites dropped from FileSystem
// join, and the tile chosen: its sprite, its collision, its data and the frames that animate it.
// Saved once a change is over.
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

[[nodiscard]] std::string tileTooltip(const ToolsState& state, const asset::TileData& tile)
{
    const asset::AssetInfo* const info = state.database != nullptr ? state.database->find(tile.sprite) : nullptr;
    return std::format("Tile {}: {}\nCollision: {}{}{}", tile.id, info != nullptr ? info->name : std::string("(no sprite)"),
                       asset::toString(tile.collision), tile.frames.empty() ? "" : "\nAnimated",
                       tile.data.empty() ? std::string{} : std::format("\nData: {}", tile.data));
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
        return std::format("{}|{}|{}|{}", editor.tileset.tiles.size(), tile != nullptr ? tile->id : 0u,
                           tile != nullptr ? tile->frames.size() : 0u, editor.error.empty());
    }

    void build(InspectorUi& ui, ToolsState& state, EditorUiKit& kit) override
    {
        const ThemeColors& colors = themeColors();
        TilesetEditor& editor = state.tilesetEditor;
        m_tiles.reset();
        m_frames.reset();
        m_sprite = {};
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
        ui.spriteGrid(kit, ui.sections[m_tilesCard], m_tiles, editor.tileset.tiles.size(), std::round(ui.font * 3.2f), true);
        ui.tooltip(m_tiles->drop, "Drop sprites from the FileSystem here, or a texture to add a tile for each of its sprites");

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
            const FormRow fpsRow = ui.formRow(card, "Frames per second");
            const std::array<std::string_view, 1> one{""};
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
        // Animated tiles play in the palette as in the game.
        const double seconds = state.input.time();
        for (std::size_t index = 0; index < tileset.tiles.size(); ++index)
        {
            const asset::TileData& tile = tileset.tiles[index];
            ui.showSprite(m_tiles->images[index], tile.spriteAt(seconds));
            ui.scene().get<scene::UiRect>(m_tiles->cells[index]).style = tile.id == editor.selectedTile ? "row_selected" : "row";
            ui.tooltip(m_tiles->cells[index], tileTooltip(state, tile));
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

        asset::TileData* const tile = [&]() -> asset::TileData* {
            const auto found = std::ranges::find(tileset.tiles, editor.selectedTile, &asset::TileData::id);
            return found != tileset.tiles.end() ? &*found : nullptr;
        }();
        if (tile != nullptr && m_sprite.isValid() && m_frames != nullptr && m_frames->cells.size() == tile->frames.size())
        {
            answerTile(ui, state, *tile);
        }
        const bool busy = world.held().isValid() || world.isEditing();
        if (m_dirty && !busy)
        {
            saveTileset(state);
            m_dirty = false;
        }
    }

private:
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
    Entity m_fps;
    Entity m_animation;
    std::unique_ptr<SpriteGrid> m_frames;
    Entity m_frameMenu;
    Button m_removeFrame;
    std::optional<std::size_t> m_menuFrame;
    Button m_removeTile;
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
