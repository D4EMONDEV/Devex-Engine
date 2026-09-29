// The page of a texture in the inspector: the image over a checkerboard, with the cells it is cut into
// and the pivot of each sprite, how it imports and how it is cut, applied by Reimport, and the ways
// to animate its sprites or to paint with them.
#include "InspectorUi.hpp"

#include <devex/asset/import/Importer.hpp>
#include <devex/core/Log.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <span>
#include <string>

namespace devex::tools::detail {
namespace {

using scene::Entity;
using scene::UiRect;
using Button = PanelButton;

struct Choice
{
    const char* option;
    const char* label;
    const char* tooltip;
};

constexpr std::array qualityChoices{
    Choice{"fast", "Fast", "Quick to import, for a first look"},
    Choice{"normal", "Normal", "The balance of import time and image quality"},
    Choice{"high", "High", "Slow to import, for the final images"},
};

constexpr std::array filterChoices{
    Choice{"linear", "Linear", "Blends the nearest pixels: smooth images"},
    Choice{"nearest", "Nearest", "Takes the nearest pixel: sharp pixel art"},
};

constexpr std::array spriteModeChoices{
    Choice{"none", "None", "A texture for materials and interfaces only"},
    Choice{"single", "Single", "The whole image is one sprite"},
    Choice{"grid", "Grid", "Cut into cells of the same size, row by row from the top left; empty cells are left out"},
};

struct PivotChoice
{
    const char* label;
    math::Vec2 pivot;
};

constexpr std::array pivotChoices{
    PivotChoice{"Center", {0.5f, 0.5f}},     PivotChoice{"Bottom", {0.5f, 0.0f}},
    PivotChoice{"Bottom Left", {0.0f, 0.0f}}, PivotChoice{"Bottom Right", {1.0f, 0.0f}},
    PivotChoice{"Top", {0.5f, 1.0f}},        PivotChoice{"Top Left", {0.0f, 1.0f}},
    PivotChoice{"Left", {0.0f, 0.5f}},       PivotChoice{"Right", {1.0f, 0.5f}},
};

// Lines and pivots drawn over the preview, at most.
constexpr std::uint32_t maxLines = 64;
constexpr std::uint32_t maxPivots = 256;

template <std::size_t Count>
[[nodiscard]] std::vector<std::string> labelsOf(const std::array<Choice, Count>& choices)
{
    std::vector<std::string> labels;
    for (const Choice& choice : choices)
    {
        labels.emplace_back(choice.label);
    }
    return labels;
}

// A row of a choice of an option: its list, the choice it shows, and its mark while it waits.
struct ChoiceRow
{
    FormRow row;
    Entity list;
};

// A row of a toggle of an option.
struct ToggleRow
{
    FormRow row;
    Entity toggle;
};

class TexturePage final : public InspectorPage
{
public:
    std::string signature(ToolsState& state) override
    {
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        if (!source)
        {
            return {};
        }
        m_settings.show(state.selectedAsset);
        const bool highDynamicRange = source->path.ends_with(".hdr");
        return std::format("{}|{}|{}|{}|{}|{}", static_cast<int>(source->status), highDynamicRange, mode(state, highDynamicRange),
                           columns(state), rows(state), source->error);
    }

    void build(InspectorUi& ui, ToolsState& state, EditorUiKit& kit) override
    {
        const ThemeColors& colors = themeColors();
        m_toggles.clear();
        m_choices.clear();
        m_lines.clear();
        m_pivots.clear();
        m_ppu = {};
        m_grid = {};
        m_pivotNumbers.clear();
        m_border.clear();
        m_count = {};
        m_newFrames = m_newTileset = Button{};
        const asset::AssetInfo* const info = state.database->find(state.selectedAsset);
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        if (info == nullptr || !source)
        {
            return;
        }
        m_highDynamicRange = source->path.ends_with(".hdr");
        m_mode = mode(state, m_highDynamicRange);
        ui.heading(kit, m_highDynamicRange ? icons::Mountain : icons::Image, m_highDynamicRange ? colors.environment : colors.texture,
                   info->name, source->path);
        if (source->status == asset::ImportStatus::Importing)
        {
            ui.note(nullptr, "Importing...");
        }
        else if (source->status == asset::ImportStatus::Failed)
        {
            ui.note(nullptr, "The file could not be imported.", "error");
            ui.note(nullptr, source->error, "dim", 3.0f);
        }

        // The image over a checkerboard, which shows what is transparent, with the cells and the
        // pivots of its sprites.
        m_preview = ui.add(ui.content, "Preview", rects::wide(1.0f));
        const UiRect corner{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {1.0f, 1.0f}};
        m_checker = ui.add(m_preview, "Checker", corner);
        ui.scene().add<scene::UiImage>(m_checker, scene::UiImage{.texture = kit.checker(), .raycastTarget = false});
        m_image = ui.add(m_preview, "Image", corner);
        ui.scene().add<scene::UiImage>(m_image, scene::UiImage{.texture = state.selectedAsset, .raycastTarget = false});
        const math::Vec4 line = linearColor(ImVec4(colors.accent.x, colors.accent.y, colors.accent.z, 0.8f));
        const std::uint32_t across = m_mode == "grid" ? std::min(columns(state), maxLines) : 1;
        const std::uint32_t down = m_mode == "grid" ? std::min(rows(state), maxLines) : 1;
        if (m_mode != "none")
        {
            // The edges of the cells, the outer ones included.
            for (std::uint32_t index = 0; index < across + 1 + down + 1; ++index)
            {
                const Entity made = ui.add(m_preview, "Line", corner);
                ui.scene().add<scene::UiImage>(made, scene::UiImage{.color = line, .raycastTarget = false});
                m_lines.push_back(made);
            }
            if (across * down <= maxPivots)
            {
                for (std::uint32_t index = 0; index < across * down; ++index)
                {
                    const Entity made = ui.add(m_preview, "Pivot", corner);
                    ui.scene().add<scene::UiImage>(made, scene::UiImage{.color = line, .cornerRadius = 4.0f, .raycastTarget = false});
                    m_pivots.push_back(made);
                }
            }
        }
        m_size = ui.note(nullptr, "");

        Section& import = ui.card(kit, "Import");
        if (!m_highDynamicRange)
        {
            addToggle(ui, import, "Color", "srgb", true, "Colors encoded in sRGB, as images are; off for data such as masks and color tables");
            addToggle(ui, import, "Normal map", "normal_map", false,
                      "Tangent-space normals: only X and Y are kept, and mip levels are normalized again");
        }
        addToggle(ui, import, "Mipmaps", "mipmaps", true,
                  "Smaller copies of the image for far away surfaces: no shimmering, a third more memory");
        if (!m_highDynamicRange)
        {
            addToggle(ui, import, "Compress", "compress", true,
                      "BC7 on the GPU: a quarter of the memory, slower to import. Pixel art keeps its pixels exactly without it.");
            addChoice(ui, import, "Quality", "quality", qualityChoices, "normal");
        }
        addChoice(ui, import, "Filter", "filter", filterChoices, "linear");
        Section* last = &import;

        if (!m_highDynamicRange)
        {
            Section& sprites = ui.card(kit, "Sprites");
            last = &sprites;
            addChoice(ui, sprites, "Mode", "sprite_mode", spriteModeChoices, "none");
            if (m_mode != "none")
            {
                m_ppuRow = ui.formRow(sprites, "Pixels per unit");
                const std::array<std::string_view, 1> one{""};
                m_ppu = ui.numbers(m_ppuRow.editor, one, {.minValue = 0.01f, .maxValue = 100000.0f, .dragSpeed = 0.5f, .decimals = 2}).front();
                ui.tooltip(m_ppuRow.editor, "How many pixels make a meter of the world");
                if (m_mode == "grid")
                {
                    m_gridRow = ui.formRow(sprites, "Columns and rows");
                    const std::array<std::string_view, 2> letters{"c", "r"};
                    const std::vector<Entity> made =
                        ui.numbers(m_gridRow.editor, letters, {.minValue = 1.0f, .maxValue = 1024.0f, .step = 1.0f, .dragSpeed = 0.1f, .decimals = 0});
                    m_grid = {made[0], made[1]};
                    ui.tooltip(m_gridRow.editor, "How many cells across and down the image is cut into");
                }
                m_pivotRow = ui.formRow(sprites, "Pivot");
                std::vector<std::string> presets;
                for (const PivotChoice& choice : pivotChoices)
                {
                    presets.emplace_back(choice.label);
                }
                m_pivotPreset = ui.choice(m_pivotRow.editor, std::move(presets));
                ui.tooltip(m_pivotRow.editor, "Where the entity stands on each sprite: the point it turns around and is placed by");
                const FormRow custom = ui.formRow(sprites, "");
                const std::array<std::string_view, 2> axes{"x", "y"};
                m_pivotNumbers = ui.numbers(custom.editor, axes, {.dragSpeed = 0.01f, .decimals = 2});
                m_borderRow = ui.formRow(sprites, "Border");
                const std::array<std::string_view, 4> sides{"l", "b", "r", "t"};
                m_border = ui.numbers(m_borderRow.editor, sides, {.minValue = 0.0f, .maxValue = 100000.0f, .step = 1.0f, .dragSpeed = 0.2f, .decimals = 0});
                ui.tooltip(m_borderRow.editor, "Pixels kept at their size by sliced and tiled sprites: left, bottom, right, top");
            }
        }
        m_footer = importFooter(ui, kit, *last);

        if (!m_highDynamicRange && m_mode != "none")
        {
            m_count = ui.note(nullptr, "", "dim", 2.0f);
            const Entity row = ui.actions(nullptr);
            m_newFrames = ui.action(kit, row, Icon::Clapperboard, "New Sprite Frames");
            ui.tooltip(m_newFrames.entity, "Sprite frames beside the texture, with one animation of all its sprites");
            m_newTileset = ui.action(kit, row, Icon::Grid, "New Tileset");
            ui.tooltip(m_newTileset.entity, "A tileset beside the texture, with a tile for each of its sprites");
        }
    }

    void sync(InspectorUi& ui, ToolsState& state, EditorUiKit& kit) override
    {
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        if (!source || !m_preview.isValid())
        {
            return;
        }
        syncPreview(ui, state);
        for (const auto& [key, entry] : m_toggles)
        {
            ui.setToggle(entry.toggle, m_settings.boolean(state, key, entry.fallback));
            ui.scene().get<UiRect>(entry.row.mark).visible = m_settings.waits(key);
        }
        for (const auto& [key, entry] : m_choices)
        {
            const std::string current = m_settings.text(state, key, entry.fallback);
            const auto chosen = std::ranges::find_if(entry.choices, [&](const Choice& choice) { return current == choice.option; });
            scene::UiDropdown& dropdown = ui.scene().get<scene::UiDropdown>(entry.list);
            dropdown.selected = chosen != entry.choices.end() ? static_cast<std::int32_t>(chosen - entry.choices.begin()) : -1;
            dropdown.placeholder = current;
            ui.tooltip(entry.list, chosen != entry.choices.end() ? chosen->tooltip : "");
            ui.scene().get<UiRect>(entry.row.mark).visible = m_settings.waits(key);
        }
        const ui::UiWorld& world = ui.panel.world();
        const auto number = [&](Entity box, float value) {
            if (box.isValid() && world.editedField() != box && world.held() != box)
            {
                ui.scene().get<scene::UiNumberField>(box).value = value;
            }
        };
        if (m_ppu.isValid())
        {
            number(m_ppu, static_cast<float>(m_settings.number(state, "pixels_per_unit", 100.0)));
            ui.scene().get<UiRect>(m_ppuRow.mark).visible = m_settings.waits("pixels_per_unit");
            const math::Vec4 pivot = m_settings.vector(state, "pivot", math::Vec4{0.5f, 0.5f, 0.0f, 0.0f});
            number(m_pivotNumbers[0], pivot.x);
            number(m_pivotNumbers[1], pivot.y);
            const auto preset = std::ranges::find_if(pivotChoices, [&](const PivotChoice& choice) {
                return choice.pivot == math::Vec2{pivot.x, pivot.y};
            });
            scene::UiDropdown& presets = ui.scene().get<scene::UiDropdown>(m_pivotPreset);
            presets.selected = preset != pivotChoices.end() ? static_cast<std::int32_t>(preset - pivotChoices.begin()) : -1;
            presets.placeholder = "Custom";
            ui.scene().get<UiRect>(m_pivotRow.mark).visible = m_settings.waits("pivot");
            const math::Vec4 border = m_settings.vector(state, "border", math::Vec4{0.0f});
            for (std::size_t index = 0; index < m_border.size(); ++index)
            {
                number(m_border[index], border[static_cast<math::Vec4::length_type>(index)]);
            }
            ui.scene().get<UiRect>(m_borderRow.mark).visible = m_settings.waits("border");
        }
        if (m_grid[0].isValid())
        {
            number(m_grid[0], static_cast<float>(columns(state)));
            number(m_grid[1], static_cast<float>(rows(state)));
            ui.scene().get<UiRect>(m_gridRow.mark).visible = m_settings.waits("columns") || m_settings.waits("rows");
        }
        if (m_count.isValid())
        {
            std::size_t sprites = 0;
            for (const asset::AssetId id : source->assets)
            {
                const asset::AssetInfo* const sprite = state.database->find(id);
                sprites += sprite != nullptr && sprite->type == asset::AssetType::Sprite ? 1 : 0;
            }
            ui.scene().get<scene::UiText>(m_count).text =
                std::format("{} {}, under the texture in the FileSystem: drag one into the view or onto a SpriteRenderer.", sprites,
                            sprites == 1 ? "sprite" : "sprites");
            ui.enable(m_newFrames, sprites > 0);
            ui.enable(m_newTileset, sprites > 0);
        }
        syncImportFooter(ui, kit, m_footer, m_settings);
    }

    void answer(InspectorUi& ui, ToolsState& state, EditorUiKit&) override
    {
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        if (!source || !m_preview.isValid())
        {
            return;
        }
        const ui::UiWorld& world = ui.panel.world();
        for (const auto& [key, entry] : m_toggles)
        {
            if (world.wasChanged(entry.toggle))
            {
                m_settings.set(state, key, ui.scene().get<scene::UiToggle>(entry.toggle).value);
            }
        }
        for (const auto& [key, entry] : m_choices)
        {
            const std::int32_t selected = ui.scene().get<scene::UiDropdown>(entry.list).selected;
            if (world.wasChanged(entry.list) && selected >= 0 && static_cast<std::size_t>(selected) < entry.choices.size())
            {
                m_settings.set(state, key, std::string(entry.choices[static_cast<std::size_t>(selected)].option));
            }
        }
        const auto value = [&](Entity box) { return ui.scene().get<scene::UiNumberField>(box).value; };
        if (m_ppu.isValid() && world.wasChanged(m_ppu) && value(m_ppu) > 0.0f)
        {
            m_settings.set(state, "pixels_per_unit", static_cast<double>(value(m_ppu)));
        }
        if (m_grid[0].isValid())
        {
            if (world.wasChanged(m_grid[0]))
            {
                m_settings.set(state, "columns", static_cast<std::int64_t>(std::max(value(m_grid[0]), 1.0f)));
            }
            if (world.wasChanged(m_grid[1]))
            {
                m_settings.set(state, "rows", static_cast<std::int64_t>(std::max(value(m_grid[1]), 1.0f)));
            }
        }
        if (m_ppu.isValid())
        {
            if (world.wasChanged(m_pivotPreset))
            {
                const std::int32_t selected = ui.scene().get<scene::UiDropdown>(m_pivotPreset).selected;
                if (selected >= 0 && static_cast<std::size_t>(selected) < pivotChoices.size())
                {
                    m_settings.set(state, "pivot", vectorValue(&pivotChoices[static_cast<std::size_t>(selected)].pivot.x, 2));
                }
            }
            if (world.wasChanged(m_pivotNumbers[0]) || world.wasChanged(m_pivotNumbers[1]))
            {
                const std::array<float, 2> pivot{value(m_pivotNumbers[0]), value(m_pivotNumbers[1])};
                m_settings.set(state, "pivot", vectorValue(pivot.data(), 2));
            }
            if (std::ranges::any_of(m_border, [&](Entity box) { return world.wasChanged(box); }))
            {
                std::array<float, 4> border{};
                for (std::size_t index = 0; index < border.size(); ++index)
                {
                    border[index] = std::max(value(m_border[index]), 0.0f);
                }
                m_settings.set(state, "border", vectorValue(border.data(), 4));
            }
        }
        answerImportFooter(ui, state, m_footer, m_settings);

        const std::string folder = source->path.substr(0, source->path.find_last_of('/'));
        if (m_newFrames.entity.isValid() && world.wasClicked(m_newFrames.entity))
        {
            if (core::Result<std::filesystem::path> created = createSpriteFramesFile(state, folder, state.selectedAsset); !created)
            {
                DEVEX_LOG_ERROR("Cannot create the sprite frames: {}", created.error());
            }
        }
        else if (m_newTileset.entity.isValid() && world.wasClicked(m_newTileset.entity))
        {
            if (core::Result<std::filesystem::path> created = createTilesetFile(state, folder, state.selectedAsset); !created)
            {
                DEVEX_LOG_ERROR("Cannot create the tileset: {}", created.error());
            }
        }
    }

private:
    struct ToggleEntry
    {
        FormRow row;
        Entity toggle;
        bool fallback = false;
    };
    struct ChoiceEntry
    {
        FormRow row;
        Entity list;
        std::span<const Choice> choices;
        std::string fallback;
    };

    [[nodiscard]] std::string mode(const ToolsState& state, bool highDynamicRange) const
    {
        return highDynamicRange ? std::string("none") : m_settings.text(state, "sprite_mode", "none");
    }
    [[nodiscard]] std::uint32_t columns(const ToolsState& state) const
    {
        return static_cast<std::uint32_t>(std::max(m_settings.number(state, "columns", 1.0), 1.0));
    }
    [[nodiscard]] std::uint32_t rows(const ToolsState& state) const
    {
        return static_cast<std::uint32_t>(std::max(m_settings.number(state, "rows", 1.0), 1.0));
    }

    void addToggle(InspectorUi& ui, Section& section, const char* label, std::string key, bool fallback, const char* tooltip)
    {
        const FormRow row = ui.formRow(section, label);
        ui.tooltip(row.editor, tooltip);
        m_toggles.emplace_back(std::move(key), ToggleEntry{.row = row, .toggle = ui.toggle(row.editor), .fallback = fallback});
    }

    template <std::size_t Count>
    void addChoice(InspectorUi& ui, Section& section, const char* label, std::string key, const std::array<Choice, Count>& choices,
                   std::string fallback)
    {
        const FormRow row = ui.formRow(section, label);
        m_choices.emplace_back(std::move(key), ChoiceEntry{.row = row,
                                                           .list = ui.choice(row.editor, labelsOf(choices)),
                                                           .choices = std::span<const Choice>(choices),
                                                           .fallback = std::move(fallback)});
    }

    // The image as large as the panel allows, whole pixels when it can, no taller than a few lines.
    void syncPreview(InspectorUi& ui, const ToolsState& state)
    {
        // Asking for the texture loads it; its size is known once it has.
        const bool requested = state.textures && state.textures(state.selectedAsset).isValid();
        const math::Extent2D size = state.textureSizes ? state.textureSizes(state.selectedAsset) : math::Extent2D{};
        const bool loaded = requested && size.width > 0 && size.height > 0;
        UiRect& preview = ui.scene().get<UiRect>(m_preview);
        preview.visible = loaded;
        std::string caption = loaded ? std::format("{} x {} pixels", size.width, size.height) : std::string("Loading the preview...");
        if (ui.scene().get<scene::UiText>(m_size).text != caption)
        {
            ui.scene().get<scene::UiText>(m_size).text = std::move(caption);
        }
        if (!loaded)
        {
            return;
        }
        const auto width = static_cast<float>(size.width);
        const auto height = static_cast<float>(size.height);
        const float available = std::max(ui.panel.size().x - ui.font * 0.6f - 8.0f, 1.0f);
        const float scale = std::min({available / width, ui.font * 16.0f / height, std::max(1.0f, std::floor(available / width))});
        const math::Vec2 extent{width * scale, height * scale};
        const math::Vec2 origin{std::floor((available - extent.x) * 0.5f), 0.0f};
        preview.offsetMax.y = preview.offsetMin.y + extent.y;
        const auto place = [&](Entity entity, math::Vec2 min, math::Vec2 max) {
            UiRect& rect = ui.scene().get<UiRect>(entity);
            rect.offsetMin = origin + min;
            rect.offsetMax = origin + max;
        };
        place(m_checker, math::Vec2{0.0f}, extent);
        place(m_image, math::Vec2{0.0f}, extent);
        if (m_lines.empty())
        {
            return;
        }
        const std::uint32_t across = m_mode == "grid" ? std::min(columns(state), maxLines) : 1;
        const std::uint32_t down = m_mode == "grid" ? std::min(rows(state), maxLines) : 1;
        const float cellWidth = m_mode == "grid" ? static_cast<float>(size.width / std::max(columns(state), 1u)) * scale : extent.x;
        const float cellHeight = m_mode == "grid" ? static_cast<float>(size.height / std::max(rows(state), 1u)) * scale : extent.y;
        std::size_t next = 0;
        for (std::uint32_t column = 0; column <= across && next < m_lines.size(); ++column, ++next)
        {
            const float x = std::min(static_cast<float>(column) * cellWidth, extent.x - 1.0f);
            place(m_lines[next], math::Vec2{x, 0.0f}, math::Vec2{x + 1.0f, std::min(static_cast<float>(down) * cellHeight, extent.y)});
        }
        for (std::uint32_t row = 0; row <= down && next < m_lines.size(); ++row, ++next)
        {
            const float y = std::min(static_cast<float>(row) * cellHeight, extent.y - 1.0f);
            place(m_lines[next], math::Vec2{0.0f, y}, math::Vec2{std::min(static_cast<float>(across) * cellWidth, extent.x), y + 1.0f});
        }
        // The pivot of each sprite, from the bottom left of its cell.
        const math::Vec4 pivot = m_settings.vector(state, "pivot", math::Vec4{0.5f, 0.5f, 0.0f, 0.0f});
        for (std::size_t index = 0; index < m_pivots.size(); ++index)
        {
            const auto column = static_cast<float>(index % across);
            const auto row = static_cast<float>(index / across);
            const math::Vec2 at{(column + pivot.x) * cellWidth, (row + 1.0f - pivot.y) * cellHeight};
            place(m_pivots[index], at - math::Vec2{3.0f}, at + math::Vec2{3.0f});
        }
    }

    ImportSettings m_settings;
    bool m_highDynamicRange = false;
    std::string m_mode;
    Entity m_preview;
    Entity m_checker;
    Entity m_image;
    std::vector<Entity> m_lines;
    std::vector<Entity> m_pivots;
    Entity m_size;
    std::vector<std::pair<std::string, ToggleEntry>> m_toggles;
    std::vector<std::pair<std::string, ChoiceEntry>> m_choices;
    FormRow m_ppuRow;
    Entity m_ppu;
    FormRow m_gridRow;
    std::array<Entity, 2> m_grid{};
    FormRow m_pivotRow;
    Entity m_pivotPreset;
    std::vector<Entity> m_pivotNumbers;
    FormRow m_borderRow;
    std::vector<Entity> m_border;
    ImportFooter m_footer;
    Entity m_count;
    Button m_newFrames;
    Button m_newTileset;
};

} // namespace

std::unique_ptr<InspectorPage> makeTexturePage()
{
    return std::make_unique<TexturePage>();
}

} // namespace devex::tools::detail
