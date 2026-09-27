#include "ToolsState.hpp"

#include <devex/asset/import/TilesetFile.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <format>
#include <string>
#include <system_error>
#include <utility>

namespace devex::tools::detail {
namespace {

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

} // namespace

// The tiles of a tileset as a grid of squares, the chosen one outlined; returns the one clicked.
std::optional<std::uint32_t> drawTilePalette(ToolsState& state, const asset::TilesetData& tileset, std::uint32_t selected,
                                             float size, bool acceptsDrops, std::vector<asset::AssetId>* dropped)
{
    std::optional<std::uint32_t> clicked;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float available = ImGui::GetContentRegionAvail().x;
    const double seconds = ImGui::GetTime();
    float lineWidth = 0.0f;
    const auto place = [&] {
        if (lineWidth > 0.0f && lineWidth + spacing + size <= available)
        {
            ImGui::SameLine();
            lineWidth += spacing;
        }
        else
        {
            lineWidth = 0.0f;
        }
        lineWidth += size;
    };
    for (const asset::TileData& tile : tileset.tiles)
    {
        place();
        ImGui::PushID(static_cast<int>(tile.id));
        ImGui::BeginGroup();
        drawSpriteThumbnail(state, tile.spriteAt(seconds), size, tile.id == selected);
        ImGui::EndGroup();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            clicked = tile.id;
        }
        const asset::AssetInfo* const info = state.database != nullptr ? state.database->find(tile.sprite) : nullptr;
        ImGui::SetItemTooltip("Tile %u: %s\nCollision: %s%s%s", tile.id, info != nullptr ? info->name.c_str() : "(no sprite)",
                              std::string(asset::toString(tile.collision)).c_str(), tile.frames.empty() ? "" : "\nAnimated",
                              tile.data.empty() ? "" : std::format("\nData: {}", tile.data).c_str());
        ImGui::PopID();
    }
    if (acceptsDrops && dropped != nullptr)
    {
        place();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("drop tiles", ImVec2(size, size));
        const ThemeColors& colors = themeColors();
        ImGui::GetWindowDrawList()->AddRect(origin, origin + ImVec2(size, size), uiColorU32(colors.textDim), 3.0f);
        const ImVec2 plusSize = ImGui::CalcTextSize(icons::Plus.c_str());
        ImGui::GetWindowDrawList()->AddText(origin + (ImVec2(size, size) - plusSize) * 0.5f, uiColorU32(colors.textDim),
                                            icons::Plus.c_str());
        ImGui::SetItemTooltip("Drop sprites from the FileSystem here, or a texture to add a tile for each of its sprites");
        *dropped = acceptDroppedSprites(state);
    }
    return clicked;
}

void drawTilesetInspector(ToolsState& state)
{
    const ThemeColors& colors = themeColors();
    const asset::AssetInfo* const info = state.database != nullptr ? state.database->find(state.selectedAsset) : nullptr;
    const std::optional<asset::SourceFile> source =
        state.database != nullptr ? state.database->sourceOf(state.selectedAsset) : std::nullopt;
    if (info == nullptr || !source)
    {
        state.selectedAsset = {};
        return;
    }
    loadTileset(state, *source);
    TilesetEditor& editor = state.tilesetEditor;
    asset::TilesetData& tileset = editor.tileset;

    ImGui::AlignTextToFramePadding();
    iconLabel(icons::Grid, colors.texture);
    boldText(info->name.c_str());
    ImGui::TextDisabled("%s", source->path.c_str());
    ImGui::Spacing();
    if (!editor.error.empty())
    {
        ImGui::TextColored(uiColor(colors.error), "The file could not be read: %s", editor.error.c_str());
        ImGui::TextWrapped("Editing the tileset writes a new one over it.");
    }

    bool changed = false;
    ImGui::SeparatorText(std::format("Tiles ({})", tileset.tiles.size()).c_str());
    std::vector<asset::AssetId> dropped;
    if (const std::optional<std::uint32_t> clicked =
            drawTilePalette(state, tileset, editor.selectedTile, ImGui::GetFontSize() * 3.2f, true, &dropped))
    {
        editor.selectedTile = *clicked;
    }
    if (!dropped.empty())
    {
        if (const std::uint32_t added = addTiles(tileset, dropped); added != 0)
        {
            editor.selectedTile = added;
            changed = true;
        }
    }

    asset::TileData* const tile = [&]() -> asset::TileData* {
        const auto found = std::ranges::find(tileset.tiles, editor.selectedTile, &asset::TileData::id);
        return found != tileset.tiles.end() ? &*found : nullptr;
    }();
    if (tile != nullptr)
    {
        ImGui::PushFont(editorFonts().bold, 0.0f);
        ImGui::SeparatorText(std::format("Tile {}", tile->id).c_str());
        ImGui::PopFont();
        if (beginProperties("tile"))
        {
            propertyName("Sprite");
            changed |= drawAssetPicker(state, "##sprite", asset::AssetType::Sprite, tile->sprite);
            propertyName("Collision");
            const auto chosen = std::ranges::find(collisionChoices, tile->collision, &CollisionChoice::collision);
            if (beginCombo("##collision", chosen != collisionChoices.end() ? chosen->label : "?"))
            {
                for (const CollisionChoice& choice : collisionChoices)
                {
                    if (ImGui::Selectable(choice.label, choice.collision == tile->collision) && choice.collision != tile->collision)
                    {
                        tile->collision = choice.collision;
                        changed = true;
                    }
                    ImGui::SetItemTooltip("%s", choice.tooltip);
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("What game code reads of the tile with Tilemaps.GetCollision; 2D physics will too");
            propertyName("Data");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##data", "water, damage=5...", &tile->data);
            ImGui::SetItemTooltip("Anything the game reads of the tile with Tilemaps.GetData");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            propertyName("Frames per second");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::DragFloat("##fps", &tile->fps, 0.1f, 0.0f, 120.0f, "%g");
            tile->fps = std::clamp(tile->fps, 0.0f, 120.0f);
            ImGui::SetItemTooltip("How fast an animated tile goes through its frames");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            endProperties();
        }

        // The frames of an animated tile, which replace its sprite while it plays.
        ImGui::TextDisabled("Animation: %zu %s", tile->frames.size(), tile->frames.size() == 1 ? "frame" : "frames");
        const float thumbnail = ImGui::GetFontSize() * 2.6f;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float available = ImGui::GetContentRegionAvail().x;
        float lineWidth = 0.0f;
        std::optional<std::size_t> removed;
        for (std::size_t index = 0; index < tile->frames.size(); ++index)
        {
            if (lineWidth > 0.0f && lineWidth + spacing + thumbnail <= available)
            {
                ImGui::SameLine();
                lineWidth += spacing;
            }
            else
            {
                lineWidth = 0.0f;
            }
            lineWidth += thumbnail;
            ImGui::PushID(static_cast<int>(index));
            ImGui::BeginGroup();
            drawSpriteThumbnail(state, tile->frames[index], thumbnail, false);
            ImGui::EndGroup();
            ImGui::SetItemTooltip("Frame %zu: right-click to remove it", index + 1);
            if (ImGui::BeginPopupContextItem("frame menu"))
            {
                if (ImGui::MenuItem("Remove"))
                {
                    removed = index;
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (lineWidth > 0.0f && lineWidth + spacing + thumbnail <= available)
        {
            ImGui::SameLine();
        }
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("drop frames", ImVec2(thumbnail, thumbnail));
        ImGui::GetWindowDrawList()->AddRect(origin, origin + ImVec2(thumbnail, thumbnail), uiColorU32(colors.textDim), 3.0f);
        const ImVec2 plusSize = ImGui::CalcTextSize(icons::Plus.c_str());
        ImGui::GetWindowDrawList()->AddText(origin + (ImVec2(thumbnail, thumbnail) - plusSize) * 0.5f,
                                            uiColorU32(colors.textDim), icons::Plus.c_str());
        ImGui::SetItemTooltip("Drop sprites here to animate the tile: water, lava, torches");
        if (std::vector<asset::AssetId> frames = acceptDroppedSprites(state); !frames.empty())
        {
            tile->frames.insert(tile->frames.end(), frames.begin(), frames.end());
            if (tile->fps <= 0.0f)
            {
                tile->fps = 6.0f;
            }
            changed = true;
        }
        if (removed)
        {
            tile->frames.erase(tile->frames.begin() + static_cast<std::ptrdiff_t>(*removed));
            changed = true;
        }

        ImGui::Spacing();
        if (labelButton(icons::Trash, "Remove Tile"))
        {
            const std::uint32_t id = tile->id;
            std::erase_if(tileset.tiles, [id](const asset::TileData& other) { return other.id == id; });
            editor.selectedTile = tileset.tiles.empty() ? 0 : tileset.tiles.front().id;
            changed = true;
        }
        ImGui::SetItemTooltip("Cells painted with it show nothing until a tile takes its number again");
    }
    ImGui::Spacing();
    ImGui::TextWrapped("A Tilemap that names this tileset paints its cells with these tiles: select it, then choose a "
                       "tool under it in the inspector.");

    if (changed)
    {
        saveTileset(state);
    }
}

core::Result<std::filesystem::path> createTilesetFile(ToolsState& state, std::string_view folder, asset::AssetId fromTexture)
{
    if (state.database == nullptr)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "no project is open");
    }
    // Only the assets folder is imported.
    const std::string assets = std::string(asset::resourceScheme) + "assets";
    if (!folder.starts_with(assets))
    {
        folder = assets;
    }
    const asset::AssetInfo* const texture = fromTexture.isValid() ? state.database->find(fromTexture) : nullptr;
    const std::string baseName = texture != nullptr ? std::format("{} Tiles", texture->name) : std::string("Tileset");
    asset::TilesetData tileset;
    if (texture != nullptr)
    {
        static_cast<void>(addTiles(tileset, spritesOfTexture(state, fromTexture)));
    }
    const std::string base = std::string(folder) + (folder.ends_with('/') ? "" : "/");
    for (int number = 1; number < 1000; ++number)
    {
        const std::string name = number == 1 ? baseName : std::format("{} {}", baseName, number);
        const std::string resource = base + name + std::string(asset::tilesetExtension);
        const std::optional<std::filesystem::path> file = state.database->project().absolutePath(resource);
        if (!file)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "{} is outside the project", resource);
        }
        std::error_code error;
        if (std::filesystem::exists(*file, error))
        {
            continue;
        }
        if (core::Result<void> written = core::writeTextFile(*file, asset::writeTilesetFile(tileset)); !written)
        {
            return std::unexpected(written.error());
        }
        state.database->refresh();
        state.assetToSelect = resource;
        return *file;
    }
    return core::makeError(core::ErrorCode::AlreadyExists, "too many tilesets are named {} in {}", baseName, folder);
}

} // namespace devex::tools::detail
