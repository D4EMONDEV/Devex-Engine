// The page of sprite frames in the inspector: their animations, the one chosen playing as the game
// will, and its frames, which sprites dropped from FileSystem join. Saved once a change is over.
#include "InspectorUi.hpp"

#include <devex/asset/import/SpriteFramesFile.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <string>
#include <system_error>
#include <utility>

namespace devex::tools::detail {
namespace {

using scene::Entity;
using Button = PanelButton;

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

class SpriteFramesPage final : public InspectorPage
{
public:
    std::string signature(ToolsState& state) override
    {
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        if (!source)
        {
            return {};
        }
        loadFrames(state, *source);
        SpriteFramesEditor& editor = state.spriteFramesEditor;
        const auto count = static_cast<int>(editor.frames.animations.size());
        editor.selectedAnimation = count == 0 ? -1 : std::clamp(editor.selectedAnimation, 0, count - 1);
        const std::size_t frames =
            editor.selectedAnimation >= 0 ? editor.frames.animations[static_cast<std::size_t>(editor.selectedAnimation)].frames.size() : 0;
        return std::format("{}|{}|{}|{}", count, editor.selectedAnimation, frames, editor.error.empty());
    }

    void build(InspectorUi& ui, ToolsState& state, EditorUiKit& kit) override
    {
        const ThemeColors& colors = themeColors();
        SpriteFramesEditor& editor = state.spriteFramesEditor;
        m_rows.clear();
        m_grid.reset();
        m_name = {};
        const asset::AssetInfo* const info = state.database->find(state.selectedAsset);
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        if (info == nullptr || !source)
        {
            return;
        }
        ui.heading(kit, icons::Clapperboard, colors.animation, info->name, source->path);
        if (!editor.error.empty())
        {
            ui.note(nullptr, std::format("The file could not be read: {}", editor.error), "error", 2.0f);
            ui.note(nullptr, "Editing the frames writes new ones over it.");
        }

        // The animations, one row each, the chosen one lit.
        Section& animations = ui.card(kit, "Animations");
        for (std::size_t index = 0; index < editor.frames.animations.size(); ++index)
        {
            AnimationRow row;
            row.row = ui.add(animations.card, "Animation", rects::wide(ui.line), "soft_row");
            ui.scene().add<scene::UiImage>(row.row);
            ui.scene().add<scene::UiButton>(row.row);
            row.name = ui.text(row.row, rects::whole(math::Vec4{ui.font * 0.6f, 0.0f, ui.font * 9.0f, 0.0f}), "", "text");
            row.summary = ui.text(row.row,
                                  scene::UiRect{.anchorMin = {1.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {-ui.font * 9.0f, 0.0f},
                                                .offsetMax = {-ui.font * 0.6f, 0.0f}},
                                  "", "dim", false, scene::TextAlign::Right);
            animations.lines.push_back(Line{.entity = row.row});
            m_rows.push_back(row);
        }
        if (editor.frames.animations.empty())
        {
            ui.note(&animations, "No animation yet.");
        }
        const Entity buttons = ui.actions(&animations);
        m_add = ui.action(kit, buttons, Icon::Plus, "Add Animation");
        m_remove = ui.action(kit, buttons, Icon::Trash, "Remove");
        if (editor.selectedAnimation < 0)
        {
            footnote(ui);
            return;
        }

        // The animation chosen: its name, its pace, and whether it loops.
        ui.card(kit, "Animation", EntityIcon{icons::Film, colors.animation});
        m_card = ui.sections.size() - 1;
        const FormRow nameRow = ui.formRow(ui.sections[m_card], "Name");
        m_name = ui.textField(nameRow.editor);
        ui.tooltip(nameRow.editor, "The name code plays it by: SpriteAnimator.Animation = \"run\"");
        const FormRow fpsRow = ui.formRow(ui.sections[m_card], "Frames per second");
        const std::array<std::string_view, 1> one{""};
        m_fps = ui.numbers(fpsRow.editor, one, {.minValue = 0.1f, .maxValue = 240.0f, .dragSpeed = 0.1f, .decimals = 2}).front();
        const FormRow loopRow = ui.formRow(ui.sections[m_card], "Loop");
        m_loop = ui.toggle(loopRow.editor);
        ui.tooltip(loopRow.editor, "Starts again after the last frame; otherwise stops on it");

        // The preview plays the animation as the game will.
        const float size = std::round(ui.font * 7.0f);
        const Entity line = ui.add(ui.sections[m_card].card, "Preview", rects::wide(size));
        ui.sections[m_card].lines.push_back(Line{.entity = line});
        const Entity box = ui.add(line, "Box",
                                  scene::UiRect{.anchorMin = {0.5f, 0.0f}, .anchorMax = {0.5f, 0.0f}, .offsetMin = {-size * 0.5f, 0.0f},
                                                .offsetMax = {size * 0.5f, size}},
                                  "list");
        ui.scene().add<scene::UiImage>(box);
        m_preview = ui.add(box, "Sprite", rects::whole(math::Vec4{4.0f}));
        ui.scene().add<scene::UiImage>(m_preview, scene::UiImage{.raycastTarget = false, .preserveAspect = true});
        const Entity controls = ui.actions(&ui.sections[m_card]);
        m_play = ui.action(kit, controls, Icon::Pause, "Pause");
        m_restart = ui.action(kit, controls, Icon::Refresh, "Restart");
        ui.tooltip(m_restart.entity, "From the first frame");
        m_position = ui.text(controls, rects::middle({ui.font * 5.0f, ui.line}), "", "dim");

        // The frames, which sprites dropped on them join.
        Section& frames = ui.card(kit, "Frames");
        const std::size_t count = editor.frames.animations[static_cast<std::size_t>(editor.selectedAnimation)].frames.size();
        ui.spriteGrid(kit, frames, m_grid, count, std::round(ui.font * 3.5f), true);
        ui.tooltip(m_grid->drop, "Drop sprites from the FileSystem here, or a texture to add all its sprites");
        m_frameMenu = ui.pageMenu("Frame menu", ui.font * 11.0f);
        m_left = ui.menuItem(kit, m_frameMenu, Icon::ArrowUpDown, "Move Left");
        m_right = ui.menuItem(kit, m_frameMenu, Icon::ArrowUpDown, "Move Right");
        m_duplicate = ui.menuItem(kit, m_frameMenu, Icon::CopyPlus, "Duplicate");
        m_removeFrame = ui.menuItem(kit, m_frameMenu, Icon::Trash, "Remove");
        for (const Entity cell : m_grid->cells)
        {
            ui.scene().get<scene::UiContextMenu>(cell).popup = ui.scene().reference(m_frameMenu);
        }
        footnote(ui);
    }

    void sync(InspectorUi& ui, ToolsState& state, EditorUiKit& kit) override
    {
        SpriteFramesEditor& editor = state.spriteFramesEditor;
        const asset::SpriteFramesData& frames = editor.frames;
        if (m_rows.size() != frames.animations.size())
        {
            return;
        }
        for (std::size_t index = 0; index < m_rows.size(); ++index)
        {
            const asset::SpriteAnimationData& animation = frames.animations[index];
            ui.scene().get<scene::UiRect>(m_rows[index].row).style =
                static_cast<int>(index) == editor.selectedAnimation ? "soft_row_selected" : "soft_row";
            ui.scene().get<scene::UiText>(m_rows[index].name).text = animation.name;
            ui.scene().get<scene::UiText>(m_rows[index].summary).text =
                std::format("{} {}, {} fps", animation.frames.size(), animation.frames.size() == 1 ? "frame" : "frames",
                            ui::formatNumber(animation.fps, 2));
        }
        ui.enable(m_remove, editor.selectedAnimation >= 0);
        if (!m_name.isValid() || editor.selectedAnimation < 0)
        {
            return;
        }
        const asset::SpriteAnimationData& animation = frames.animations[static_cast<std::size_t>(editor.selectedAnimation)];
        const ui::UiWorld& world = ui.panel.world();
        ui.scene().get<scene::UiText>(ui.sections[m_card].title).text = animation.name;
        if (world.editedField() != m_name)
        {
            ui.scene().get<scene::UiText>(m_name).text = animation.name;
        }
        if (world.editedField() != m_fps && world.held() != m_fps)
        {
            ui.scene().get<scene::UiNumberField>(m_fps).value = animation.fps;
        }
        ui.setToggle(m_loop, animation.loop);

        // The frame shown: the one the preview reached, or the one chosen while it is paused.
        const std::size_t count = animation.frames.size();
        if (editor.previewPlaying)
        {
            editor.previewTime += ImGui::GetIO().DeltaTime;
        }
        std::size_t shown = 0;
        if (count > 0)
        {
            shown = static_cast<std::size_t>(std::floor(editor.previewTime * animation.fps));
            shown = animation.loop ? shown % count : std::min(shown, count - 1);
            if (editor.selectedFrame >= 0 && !editor.previewPlaying)
            {
                shown = std::min(static_cast<std::size_t>(editor.selectedFrame), count - 1);
            }
        }
        ui.showSprite(m_preview, count > 0 ? animation.frames[shown] : asset::AssetId{});
        ui.relabel(kit, m_play, editor.previewPlaying ? "Pause" : "Play");
        ui.scene().get<scene::UiImage>(m_play.icon).texture = kit.icon(editor.previewPlaying ? Icon::Pause : Icon::Play);
        ui.enable(m_play, count > 0);
        ui.enable(m_restart, count > 0);
        ui.scene().get<scene::UiText>(m_position).text = count > 0 ? std::format("{} / {}", shown + 1, count) : std::string("0 / 0");

        if (m_grid != nullptr && m_grid->cells.size() == count)
        {
            for (std::size_t index = 0; index < count; ++index)
            {
                ui.showSprite(m_grid->images[index], animation.frames[index]);
                ui.scene().get<scene::UiRect>(m_grid->cells[index]).style =
                    static_cast<int>(index) == editor.selectedFrame ? "row_selected" : "row";
                const asset::AssetInfo* const frameInfo = state.database->find(animation.frames[index]);
                ui.tooltip(m_grid->cells[index], std::format("{}: {}\nRight-click for more; drop sprites here to insert them after it", index + 1,
                                                             frameInfo != nullptr ? frameInfo->name : std::string("(missing sprite)")));
            }
        }
        const std::optional<std::size_t> target = m_menuFrame;
        ui.enable(m_left, target && *target > 0);
        ui.enable(m_right, target && *target + 1 < count);
        ui.fitMenu(m_frameMenu, ui.font * 11.0f);
    }

    void answer(InspectorUi& ui, ToolsState& state, EditorUiKit&) override
    {
        SpriteFramesEditor& editor = state.spriteFramesEditor;
        asset::SpriteFramesData& frames = editor.frames;
        if (m_rows.size() != frames.animations.size())
        {
            return;
        }
        const ui::UiWorld& world = ui.panel.world();
        for (std::size_t index = 0; index < m_rows.size(); ++index)
        {
            if (world.wasClicked(m_rows[index].row))
            {
                editor.selectedAnimation = static_cast<int>(index);
                editor.selectedFrame = -1;
                editor.previewTime = 0.0f;
            }
        }
        if (world.wasClicked(m_add.entity))
        {
            frames.animations.push_back({.name = freeName(frames, frames.animations.empty() ? "idle" : "animation")});
            editor.selectedAnimation = static_cast<int>(frames.animations.size()) - 1;
            editor.selectedFrame = -1;
            m_dirty = true;
        }
        else if (world.wasClicked(m_remove.entity) && editor.selectedAnimation >= 0)
        {
            frames.animations.erase(frames.animations.begin() + editor.selectedAnimation);
            editor.selectedAnimation = std::max(0, editor.selectedAnimation - 1);
            editor.selectedFrame = -1;
            m_dirty = true;
        }
        if (m_name.isValid() && editor.selectedAnimation >= 0 && editor.selectedAnimation < static_cast<int>(frames.animations.size()) &&
            m_grid != nullptr)
        {
            answerAnimation(ui, state, frames.animations[static_cast<std::size_t>(editor.selectedAnimation)]);
        }
        const bool busy = world.held().isValid() || world.isEditing();
        if (m_dirty && !busy)
        {
            saveFrames(state);
            m_dirty = false;
        }
    }

private:
    struct AnimationRow
    {
        Entity row;
        Entity name;
        Entity summary;
    };

    void footnote(InspectorUi& ui)
    {
        ui.note(nullptr, "A SpriteAnimator that names these frames shows their animations on the SpriteRenderer of its entity.", "dim",
                2.0f);
    }

    void answerAnimation(InspectorUi& ui, ToolsState& state, asset::SpriteAnimationData& animation)
    {
        SpriteFramesEditor& editor = state.spriteFramesEditor;
        asset::SpriteFramesData& frames = editor.frames;
        const ui::UiWorld& world = ui.panel.world();
        // The name, once the field is left; another animation may not have it.
        if (world.editedField() == m_name)
        {
            m_naming = true;
        }
        else if (std::exchange(m_naming, false) && !ui.panel.input().cancelPressed)
        {
            const std::string typed = ui.scene().get<scene::UiText>(m_name).text;
            if (!typed.empty() && typed != animation.name)
            {
                if (frames.find(typed) == nullptr)
                {
                    animation.name = typed;
                    m_dirty = true;
                }
                else
                {
                    DEVEX_LOG_WARNING("Another animation is already named \"{}\"", typed);
                }
            }
        }
        if (world.wasChanged(m_fps))
        {
            animation.fps = std::clamp(ui.scene().get<scene::UiNumberField>(m_fps).value, 0.1f, 240.0f);
            m_dirty = true;
        }
        if (world.wasChanged(m_loop))
        {
            animation.loop = ui.scene().get<scene::UiToggle>(m_loop).value;
            m_dirty = true;
        }
        if (world.wasClicked(m_play.entity))
        {
            editor.previewPlaying = !editor.previewPlaying;
        }
        else if (world.wasClicked(m_restart.entity))
        {
            editor.previewTime = 0.0f;
        }

        // Frames chosen, dropped on, and changed from their menu.
        const std::size_t count = animation.frames.size();
        if (m_grid->cells.size() != count)
        {
            return;
        }
        std::optional<std::pair<std::size_t, std::vector<asset::AssetId>>> inserted;
        for (std::size_t index = 0; index < count; ++index)
        {
            if (world.wasClicked(m_grid->cells[index]))
            {
                editor.selectedFrame = static_cast<int>(index);
                editor.previewPlaying = false;
            }
            if (world.wasDropped(m_grid->cells[index]) && world.dropped() != nullptr)
            {
                inserted = std::pair{index + 1, droppedSprites(state, *world.dropped())};
            }
        }
        if (world.wasDropped(m_grid->drop) && world.dropped() != nullptr)
        {
            inserted = std::pair{count, droppedSprites(state, *world.dropped())};
        }
        if (ui.panel.input().secondaryPressed && world.isPopupOpen(ui.scene(), m_frameMenu))
        {
            const auto found = std::ranges::find(m_grid->cells, world.contextTarget());
            m_menuFrame = found != m_grid->cells.end() ? std::optional(static_cast<std::size_t>(found - m_grid->cells.begin())) : std::nullopt;
        }
        if (m_menuFrame && *m_menuFrame < count)
        {
            const std::size_t index = *m_menuFrame;
            if (world.wasClicked(m_left.entity) && index > 0)
            {
                std::swap(animation.frames[index - 1], animation.frames[index]);
                editor.selectedFrame = static_cast<int>(index - 1);
                m_dirty = true;
            }
            else if (world.wasClicked(m_right.entity) && index + 1 < count)
            {
                std::swap(animation.frames[index], animation.frames[index + 1]);
                editor.selectedFrame = static_cast<int>(index + 1);
                m_dirty = true;
            }
            else if (world.wasClicked(m_duplicate.entity))
            {
                inserted = std::pair{index + 1, std::vector<asset::AssetId>{animation.frames[index]}};
            }
            else if (world.wasClicked(m_removeFrame.entity))
            {
                animation.frames.erase(animation.frames.begin() + static_cast<std::ptrdiff_t>(index));
                editor.selectedFrame = -1;
                m_dirty = true;
            }
        }
        if (!world.isPopupOpen(ui.scene(), m_frameMenu))
        {
            m_menuFrame.reset();
        }
        if (inserted && !inserted->second.empty())
        {
            animation.frames.insert(animation.frames.begin() + static_cast<std::ptrdiff_t>(inserted->first), inserted->second.begin(),
                                    inserted->second.end());
            m_dirty = true;
        }
    }

    std::vector<AnimationRow> m_rows;
    Button m_add;
    Button m_remove;
    std::size_t m_card = 0;
    Entity m_name;
    bool m_naming = false;
    Entity m_fps;
    Entity m_loop;
    Entity m_preview;
    Button m_play;
    Button m_restart;
    Entity m_position;
    std::unique_ptr<SpriteGrid> m_grid;
    Entity m_frameMenu;
    Button m_left;
    Button m_right;
    Button m_duplicate;
    Button m_removeFrame;
    std::optional<std::size_t> m_menuFrame;
    bool m_dirty = false;
};

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

std::unique_ptr<InspectorPage> makeSpriteFramesPage()
{
    return std::make_unique<SpriteFramesPage>();
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
