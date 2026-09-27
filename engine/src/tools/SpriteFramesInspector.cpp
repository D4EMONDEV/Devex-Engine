#include "ToolsState.hpp"

#include <devex/asset/import/SpriteFramesFile.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <string>
#include <system_error>
#include <utility>

namespace devex::tools::detail {
namespace {

[[nodiscard]] std::filesystem::file_time_type writeTime(const std::filesystem::path& file)
{
    std::error_code error;
    const std::filesystem::file_time_type time = std::filesystem::last_write_time(file, error);
    return error ? std::filesystem::file_time_type{} : time;
}

// Reads the frames of the selected asset from their file, when another asset is selected or the
// file changed outside the editor.
void loadFrames(ToolsState& state, const asset::SourceFile& source)
{
    SpriteFramesEditor& editor = state.spriteFramesEditor;
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
    core::Result<asset::SpriteFramesData> frames =
        text ? asset::parseSpriteFramesFile(*text) : core::Result<asset::SpriteFramesData>(std::unexpected(text.error()));
    if (!frames)
    {
        editor.error = frames.error().message;
        editor.frames = {};
    }
    else
    {
        editor.frames = std::move(*frames);
    }
    if (!sameAsset)
    {
        editor.selectedAnimation = 0;
        editor.selectedFrame = -1;
        editor.previewTime = 0.0f;
    }
}

void saveFrames(ToolsState& state)
{
    SpriteFramesEditor& editor = state.spriteFramesEditor;
    if (core::Result<void> valid = asset::validate(editor.frames); !valid)
    {
        DEVEX_LOG_WARNING("The sprite frames are not saved: {}", valid.error());
        return;
    }
    if (core::Result<void> written = core::writeTextFile(editor.file, asset::writeSpriteFramesFile(editor.frames)); !written)
    {
        DEVEX_LOG_ERROR("Cannot save the sprite frames: {}", written.error());
        return;
    }
    editor.fileTime = writeTime(editor.file);
    editor.error.clear();
    if (core::Result<void> queued = state.database->reimport(editor.asset); !queued)
    {
        DEVEX_LOG_WARNING("{}", queued.error());
    }
}

// A name no animation has yet: the base, then the base followed by a number.
[[nodiscard]] std::string freeName(const asset::SpriteFramesData& frames, std::string_view base)
{
    std::string name(base);
    for (int number = 2; frames.find(name) != nullptr; ++number)
    {
        name = std::format("{} {}", base, number);
    }
    return name;
}

} // namespace

// The sprites a texture was cut into, in the order of its cells.
std::vector<asset::AssetId> spritesOfTexture(const ToolsState& state, asset::AssetId texture)
{
    std::vector<asset::AssetId> sprites;
    const std::optional<asset::SourceFile> source = state.database->sourceOf(texture);
    if (!source)
    {
        return sprites;
    }
    for (const asset::AssetId id : source->assets)
    {
        const asset::AssetInfo* const info = state.database->find(id);
        if (info != nullptr && info->type == asset::AssetType::Sprite)
        {
            sprites.push_back(id);
        }
    }
    return sprites;
}

// Sprites or the sprites of a texture dropped on the last item.
std::vector<asset::AssetId> acceptDroppedSprites(const ToolsState& state)
{
    if (const std::optional<asset::AssetId> sprite = acceptDroppedAsset(asset::AssetType::Sprite))
    {
        return {*sprite};
    }
    if (const std::optional<asset::AssetId> texture = acceptDroppedAsset(asset::AssetType::Texture))
    {
        return spritesOfTexture(state, *texture);
    }
    return {};
}

void drawSpriteThumbnail(ToolsState& state, asset::AssetId sprite, float size, bool selected)
{
    const ThemeColors& colors = themeColors();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, origin + ImVec2(size, size), uiColorU32(colors.field), 3.0f);
    const std::shared_ptr<const asset::SpriteData> data = state.sprites && sprite.isValid() ? state.sprites(sprite) : nullptr;
    const render::TextureHandle texture = data != nullptr && state.textures ? state.textures(data->texture) : render::TextureHandle{};
    const std::uint64_t image = texture.isValid() ? state.renderer.imguiTexture(texture) : 0;
    if (image != 0)
    {
        // The sprite fits the square, keeping its shape.
        const float width = static_cast<float>(data->width);
        const float height = static_cast<float>(data->height);
        const float scale = (size - 4.0f) / std::max(width, height);
        const ImVec2 extent(width * scale, height * scale);
        const ImVec2 min = origin + (ImVec2(size, size) - extent) * 0.5f;
        const math::Vec4 uv = data->uvRect();
        draw->AddImage(ImTextureRef(static_cast<ImTextureID>(image)), min, min + extent, ImVec2(uv.x, uv.y), ImVec2(uv.z, uv.w));
    }
    else
    {
        const char* const text = sprite.isValid() ? "..." : "?";
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        draw->AddText(origin + (ImVec2(size, size) - textSize) * 0.5f, uiColorU32(colors.textDim), text);
    }
    if (selected)
    {
        draw->AddRect(origin, origin + ImVec2(size, size), uiColorU32(colors.accent), 3.0f, 0, 2.0f);
    }
    ImGui::Dummy(ImVec2(size, size));
}

void drawSpriteFramesInspector(ToolsState& state)
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
    loadFrames(state, *source);
    SpriteFramesEditor& editor = state.spriteFramesEditor;
    asset::SpriteFramesData& frames = editor.frames;

    ImGui::AlignTextToFramePadding();
    iconLabel(icons::Clapperboard, colors.animation);
    boldText(info->name.c_str());
    ImGui::TextDisabled("%s", source->path.c_str());
    ImGui::Spacing();
    if (!editor.error.empty())
    {
        ImGui::TextColored(uiColor(colors.error), "The file could not be read: %s", editor.error.c_str());
        ImGui::TextWrapped("Editing the frames writes new ones over it.");
    }

    bool changed = false;
    ImGui::SeparatorText("Animations");
    for (std::size_t index = 0; index < frames.animations.size(); ++index)
    {
        const asset::SpriteAnimationData& animation = frames.animations[index];
        ImGui::PushID(static_cast<int>(index));
        const std::string label = std::format("{}##animation", animation.name);
        if (ImGui::Selectable(label.c_str(), static_cast<int>(index) == editor.selectedAnimation))
        {
            editor.selectedAnimation = static_cast<int>(index);
            editor.selectedFrame = -1;
            editor.previewTime = 0.0f;
        }
        ImGui::SameLine();
        alignRight(ImGui::CalcTextSize("000 frames, 00 fps").x);
        ImGui::TextDisabled("%zu %s, %g fps", animation.frames.size(), animation.frames.size() == 1 ? "frame" : "frames",
                            static_cast<double>(animation.fps));
        ImGui::PopID();
    }
    if (frames.animations.empty())
    {
        ImGui::TextDisabled("No animation yet.");
    }
    if (labelButton(icons::Plus, "Add Animation"))
    {
        frames.animations.push_back({.name = freeName(frames, frames.animations.empty() ? "idle" : "animation")});
        editor.selectedAnimation = static_cast<int>(frames.animations.size()) - 1;
        editor.selectedFrame = -1;
        changed = true;
    }
    const bool hasSelection =
        editor.selectedAnimation >= 0 && editor.selectedAnimation < static_cast<int>(frames.animations.size());
    ImGui::SameLine();
    if (labelButton(icons::Trash, "Remove", 0.0f, hasSelection))
    {
        frames.animations.erase(frames.animations.begin() + editor.selectedAnimation);
        editor.selectedAnimation = std::max(0, editor.selectedAnimation - 1);
        editor.selectedFrame = -1;
        changed = true;
    }

    if (editor.selectedAnimation >= 0 && editor.selectedAnimation < static_cast<int>(frames.animations.size()))
    {
        asset::SpriteAnimationData& animation = frames.animations[static_cast<std::size_t>(editor.selectedAnimation)];
        ImGui::PushFont(editorFonts().bold, 0.0f);
        ImGui::SeparatorText(animation.name.c_str());
        ImGui::PopFont();
        if (beginProperties("animation"))
        {
            propertyName("Name");
            ImGui::SetNextItemWidth(-FLT_MIN);
            // The name follows the animation until it is being typed.
            if (ImGui::GetActiveID() != ImGui::GetID("##name"))
            {
                editor.renaming = animation.name;
            }
            ImGui::InputText("##name", &editor.renaming);
            ImGui::SetItemTooltip("The name code plays it by: SpriteAnimator.Animation = \"run\"");
            if (ImGui::IsItemDeactivatedAfterEdit() && !editor.renaming.empty() && editor.renaming != animation.name)
            {
                if (frames.find(editor.renaming) == nullptr)
                {
                    animation.name = editor.renaming;
                    changed = true;
                }
                else
                {
                    DEVEX_LOG_WARNING("Another animation is already named \"{}\"", editor.renaming);
                }
            }
            propertyName("Frames per second");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::DragFloat("##fps", &animation.fps, 0.1f, 0.1f, 240.0f, "%g");
            animation.fps = std::clamp(animation.fps, 0.1f, 240.0f);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            propertyName("Loop");
            changed |= ImGui::Checkbox("##loop", &animation.loop);
            ImGui::SetItemTooltip("Starts again after the last frame; otherwise stops on it");
            endProperties();
        }

        // The preview plays the animation as the game will.
        const std::size_t count = animation.frames.size();
        if (count > 0)
        {
            if (editor.previewPlaying)
            {
                editor.previewTime += ImGui::GetIO().DeltaTime;
            }
            auto shown = static_cast<std::size_t>(std::floor(editor.previewTime * animation.fps));
            shown = animation.loop ? shown % count : std::min(shown, count - 1);
            if (editor.selectedFrame >= 0 && !editor.previewPlaying)
            {
                shown = std::min(static_cast<std::size_t>(editor.selectedFrame), count - 1);
            }
            const float previewSize = ImGui::GetFontSize() * 7.0f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - previewSize) * 0.5f);
            drawSpriteThumbnail(state, animation.frames[shown], previewSize, false);
            const float buttonsWidth = toolButtonWidth() * 2.0f + ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - buttonsWidth) * 0.5f);
            if (toolButton("play", editor.previewPlaying ? icons::Pause : icons::Play,
                           editor.previewPlaying ? "Pause the preview" : "Play the preview", editor.previewPlaying))
            {
                editor.previewPlaying = !editor.previewPlaying;
            }
            ImGui::SameLine();
            if (toolButton("restart", icons::Refresh, "From the first frame"))
            {
                editor.previewTime = 0.0f;
            }
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%zu / %zu", shown + 1, count);
        }

        ImGui::SeparatorText("Frames");
        const float thumbnail = ImGui::GetFontSize() * 3.5f;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float available = ImGui::GetContentRegionAvail().x;
        float lineWidth = 0.0f;
        std::optional<std::size_t> removed;
        std::optional<std::pair<std::size_t, std::size_t>> swapped;
        std::optional<std::pair<std::size_t, std::vector<asset::AssetId>>> inserted;
        for (std::size_t index = 0; index < count; ++index)
        {
            ImGui::PushID(static_cast<int>(index));
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
            ImGui::BeginGroup();
            drawSpriteThumbnail(state, animation.frames[index], thumbnail, static_cast<int>(index) == editor.selectedFrame);
            ImGui::EndGroup();
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                editor.selectedFrame = static_cast<int>(index);
                editor.previewPlaying = false;
            }
            const asset::AssetInfo* const frameInfo = state.database->find(animation.frames[index]);
            ImGui::SetItemTooltip("%zu: %s\nRight-click for more; drop sprites here to insert them after it", index + 1,
                                  frameInfo != nullptr ? frameInfo->name.c_str() : "(missing sprite)");
            if (std::vector<asset::AssetId> dropped = acceptDroppedSprites(state); !dropped.empty())
            {
                inserted = std::pair{index + 1, std::move(dropped)};
            }
            if (ImGui::BeginPopupContextItem("frame menu"))
            {
                if (ImGui::MenuItem("Move Left", nullptr, false, index > 0))
                {
                    swapped = std::pair{index - 1, index};
                }
                if (ImGui::MenuItem("Move Right", nullptr, false, index + 1 < count))
                {
                    swapped = std::pair{index, index + 1};
                }
                if (ImGui::MenuItem("Duplicate"))
                {
                    inserted = std::pair{index + 1, std::vector<asset::AssetId>{animation.frames[index]}};
                }
                if (ImGui::MenuItem("Remove"))
                {
                    removed = index;
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        // A box at the end takes the sprites dropped on it.
        if (lineWidth > 0.0f && lineWidth + spacing + thumbnail <= available)
        {
            ImGui::SameLine();
        }
        const ImVec2 dropOrigin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("drop", ImVec2(thumbnail, thumbnail));
        ImGui::GetWindowDrawList()->AddRect(dropOrigin, dropOrigin + ImVec2(thumbnail, thumbnail), uiColorU32(colors.textDim), 3.0f);
        const ImVec2 plusSize = ImGui::CalcTextSize(icons::Plus.c_str());
        ImGui::GetWindowDrawList()->AddText(dropOrigin + (ImVec2(thumbnail, thumbnail) - plusSize) * 0.5f,
                                            uiColorU32(colors.textDim), icons::Plus.c_str());
        ImGui::SetItemTooltip("Drop sprites from the FileSystem here, or a texture to add all its sprites");
        if (std::vector<asset::AssetId> dropped = acceptDroppedSprites(state); !dropped.empty())
        {
            inserted = std::pair{count, std::move(dropped)};
        }

        if (swapped)
        {
            std::swap(animation.frames[swapped->first], animation.frames[swapped->second]);
            editor.selectedFrame = static_cast<int>(editor.selectedFrame == static_cast<int>(swapped->first) ? swapped->second
                                                                                                            : swapped->first);
            changed = true;
        }
        if (removed)
        {
            animation.frames.erase(animation.frames.begin() + static_cast<std::ptrdiff_t>(*removed));
            editor.selectedFrame = -1;
            changed = true;
        }
        if (inserted)
        {
            animation.frames.insert(animation.frames.begin() + static_cast<std::ptrdiff_t>(inserted->first),
                                    inserted->second.begin(), inserted->second.end());
            changed = true;
        }
    }
    ImGui::Spacing();
    ImGui::TextWrapped("A SpriteAnimator that names these frames shows their animations on the SpriteRenderer of its entity.");

    if (changed)
    {
        saveFrames(state);
    }
}

core::Result<std::filesystem::path> createSpriteFramesFile(ToolsState& state, std::string_view folder,
                                                           asset::AssetId fromTexture)
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
    // Named after the texture it animates, or Sprite Frames, then with a number.
    const asset::AssetInfo* const texture = fromTexture.isValid() ? state.database->find(fromTexture) : nullptr;
    const std::string baseName = texture != nullptr ? std::format("{} Frames", texture->name) : std::string("Sprite Frames");
    asset::SpriteFramesData frames;
    if (texture != nullptr)
    {
        frames.animations.push_back({.name = "default", .fps = 10.0f, .loop = true, .frames = spritesOfTexture(state, fromTexture)});
    }
    const std::string base = std::string(folder) + (folder.ends_with('/') ? "" : "/");
    for (int number = 1; number < 1000; ++number)
    {
        const std::string name = number == 1 ? baseName : std::format("{} {}", baseName, number);
        const std::string resource = base + name + std::string(asset::spriteFramesExtension);
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
        if (core::Result<void> written = core::writeTextFile(*file, asset::writeSpriteFramesFile(frames)); !written)
        {
            return std::unexpected(written.error());
        }
        state.database->refresh();
        state.assetToSelect = resource;
        return *file;
    }
    return core::makeError(core::ErrorCode::AlreadyExists, "too many sprite frames are named {} in {}", baseName, folder);
}

} // namespace devex::tools::detail
