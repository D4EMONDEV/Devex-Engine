#include "ToolsState.hpp"

#include <devex/asset/import/Importer.hpp>
#include <devex/core/Log.hpp>

#include <algorithm>
#include <array>
#include <cfloat>
#include <format>
#include <optional>
#include <string>

namespace devex::tools::detail {
namespace {

using serialization::TextValue;

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

void setOption(ToolsState& state, const asset::AssetInfo& info, std::string_view key, TextValue value)
{
    if (core::Result<void> changed = state.database->setImportOption(state.selectedAsset, key, std::move(value)); !changed)
    {
        DEVEX_LOG_ERROR("Cannot change how {} imports: {}", info.name, changed.error());
    }
}

[[nodiscard]] bool boolOption(const ToolsState& state, std::string_view key, bool fallback)
{
    const std::optional<TextValue> value = state.database->importOption(state.selectedAsset, key);
    return value ? serialization::asBool(*value).value_or(fallback) : fallback;
}

[[nodiscard]] double numberOption(const ToolsState& state, std::string_view key, double fallback)
{
    const std::optional<TextValue> value = state.database->importOption(state.selectedAsset, key);
    return value ? serialization::asNumber(*value).value_or(fallback) : fallback;
}

[[nodiscard]] std::string stringOption(const ToolsState& state, std::string_view key, std::string_view fallback)
{
    const std::optional<TextValue> value = state.database->importOption(state.selectedAsset, key);
    const std::string* const text = value ? serialization::asString(*value) : nullptr;
    return text != nullptr ? *text : std::string(fallback);
}

[[nodiscard]] math::Vec4 vectorOption(const ToolsState& state, std::string_view key, math::Vec4 fallback)
{
    const std::optional<TextValue> value = state.database->importOption(state.selectedAsset, key);
    const auto* const call = value ? std::get_if<serialization::TextCall>(&*value) : nullptr;
    if (call == nullptr)
    {
        return fallback;
    }
    for (std::size_t index = 0; index < std::min<std::size_t>(call->arguments.size(), 4); ++index)
    {
        fallback[static_cast<int>(index)] =
            static_cast<float>(serialization::asNumber(call->arguments[index]).value_or(fallback[static_cast<int>(index)]));
    }
    return fallback;
}

[[nodiscard]] TextValue vectorValue(const float* values, int count)
{
    std::vector<TextValue> arguments;
    for (int index = 0; index < count; ++index)
    {
        arguments.emplace_back(std::stod(std::format("{}", values[index])));
    }
    return serialization::makeCall(std::format("vec{}", count), std::move(arguments));
}

// A combo of the choices of a string option.
void choiceOption(ToolsState& state, const asset::AssetInfo& info, const char* id, std::string_view key,
                  std::span<const Choice> choices, std::string_view fallback)
{
    const std::string current = stringOption(state, key, fallback);
    const auto chosen = std::ranges::find_if(choices, [&](const Choice& choice) { return current == choice.option; });
    if (beginCombo(id, chosen != choices.end() ? chosen->label : current.c_str()))
    {
        for (const Choice& choice : choices)
        {
            if (ImGui::Selectable(choice.label, current == choice.option) && current != choice.option)
            {
                setOption(state, info, key, std::string(choice.option));
            }
            ImGui::SetItemTooltip("%s", choice.tooltip);
        }
        ImGui::EndCombo();
    }
    if (chosen != choices.end())
    {
        ImGui::SetItemTooltip("%s", chosen->tooltip);
    }
}

void checkboxOption(ToolsState& state, const asset::AssetInfo& info, const char* name, const char* id, std::string_view key,
                    bool fallback, const char* tooltip)
{
    propertyName(name);
    bool value = boolOption(state, key, fallback);
    if (ImGui::Checkbox(id, &value))
    {
        setOption(state, info, key, value);
    }
    ImGui::SetItemTooltip("%s", tooltip);
}

// The image, as large as the panel allows, with the cells of a grid and the pivot of each sprite.
void drawPreview(ToolsState& state, asset::AssetId texture, const std::string& mode, std::uint32_t columns,
                 std::uint32_t rows, math::Vec2 pivot)
{
    const render::TextureHandle handle = state.textures ? state.textures(texture) : render::TextureHandle{};
    const std::uint64_t image = handle.isValid() ? state.renderer.imguiTexture(handle) : 0;
    const math::Extent2D size = state.textureSizes ? state.textureSizes(texture) : math::Extent2D{};
    if (image == 0 || size.width == 0 || size.height == 0)
    {
        ImGui::TextDisabled("Loading the preview...");
        return;
    }
    const float available = ImGui::GetContentRegionAvail().x;
    const float scale = std::min({available / static_cast<float>(size.width), ImGui::GetFontSize() * 16.0f / static_cast<float>(size.height),
                                  std::max(1.0f, std::floor(available / static_cast<float>(size.width)))});
    const ImVec2 extent(static_cast<float>(size.width) * scale, static_cast<float>(size.height) * scale);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    // A checkerboard shows what is transparent.
    const float cell = ImGui::GetFontSize() * 0.5f;
    for (float y = 0.0f; y < extent.y; y += cell)
    {
        for (float x = 0.0f; x < extent.x; x += cell)
        {
            const bool dark = (static_cast<int>(x / cell) + static_cast<int>(y / cell)) % 2 == 0;
            draw->AddRectFilled(origin + ImVec2(x, y), origin + ImVec2(std::min(x + cell, extent.x), std::min(y + cell, extent.y)),
                                dark ? IM_COL32(60, 60, 60, 255) : IM_COL32(90, 90, 90, 255));
        }
    }
    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(image)), extent);
    const ThemeColors& colors = themeColors();
    const ImU32 line = uiColorU32(ImVec4(colors.accent.x, colors.accent.y, colors.accent.z, 0.8f));
    const auto pivotAt = [&](ImVec2 min, ImVec2 max) {
        const ImVec2 point(min.x + (max.x - min.x) * pivot.x, max.y - (max.y - min.y) * pivot.y);
        draw->AddCircle(point, 3.0f, line, 8, 1.5f);
    };
    if (mode == "grid")
    {
        const float cellWidth = static_cast<float>(size.width / std::max(columns, 1u)) * scale;
        const float cellHeight = static_cast<float>(size.height / std::max(rows, 1u)) * scale;
        for (std::uint32_t row = 0; row < rows; ++row)
        {
            for (std::uint32_t column = 0; column < columns; ++column)
            {
                const ImVec2 min = origin + ImVec2(static_cast<float>(column) * cellWidth, static_cast<float>(row) * cellHeight);
                const ImVec2 max = min + ImVec2(cellWidth, cellHeight);
                draw->AddRect(min, max, line);
                pivotAt(min, max);
            }
        }
    }
    else if (mode == "single")
    {
        draw->AddRect(origin, origin + extent, line);
        pivotAt(origin, origin + extent);
    }
    ImGui::TextDisabled("%u x %u pixels", size.width, size.height);
}

} // namespace

void drawTextureInspector(ToolsState& state)
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
    const bool highDynamicRange = source->path.ends_with(".hdr");

    ImGui::AlignTextToFramePadding();
    iconLabel(highDynamicRange ? icons::Mountain : icons::Image, highDynamicRange ? colors.environment : colors.texture);
    boldText(info->name.c_str());
    ImGui::TextDisabled("%s", source->path.c_str());
    ImGui::Spacing();
    if (labelButton(icons::Refresh, "Reimport"))
    {
        if (core::Result<void> queued = state.database->reimport(state.selectedAsset); !queued)
        {
            DEVEX_LOG_WARNING("{}", queued.error());
        }
    }
    if (source->status == asset::ImportStatus::Importing)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("Importing...");
    }
    else if (source->status == asset::ImportStatus::Failed)
    {
        ImGui::TextColored(uiColor(colors.error), "The file could not be imported.");
        ImGui::TextWrapped("%s", source->error.c_str());
    }
    ImGui::Spacing();

    const std::string mode = highDynamicRange ? std::string("none") : stringOption(state, "sprite_mode", "none");
    const auto columns = static_cast<std::uint32_t>(std::max(numberOption(state, "columns", 1.0), 1.0));
    const auto rows = static_cast<std::uint32_t>(std::max(numberOption(state, "rows", 1.0), 1.0));
    const math::Vec4 pivot = vectorOption(state, "pivot", math::Vec4{0.5f, 0.5f, 0.0f, 0.0f});
    drawPreview(state, state.selectedAsset, mode, columns, rows, math::Vec2{pivot.x, pivot.y});

    ImGui::SeparatorText("Import");
    if (beginProperties("texture import"))
    {
        if (!highDynamicRange)
        {
            checkboxOption(state, *info, "Color", "##srgb", "srgb", true,
                           "Colors encoded in sRGB, as images are; off for data such as masks and color tables");
            checkboxOption(state, *info, "Normal map", "##normal", "normal_map", false,
                           "Tangent-space normals: only X and Y are kept, and mip levels are normalized again");
        }
        checkboxOption(state, *info, "Mipmaps", "##mipmaps", "mipmaps", true,
                       "Smaller copies of the image for far away surfaces: no shimmering, a third more memory");
        if (!highDynamicRange)
        {
            checkboxOption(state, *info, "Compress", "##compress", "compress", true,
                           "BC7 on the GPU: a quarter of the memory, slower to import. Pixel art keeps its pixels "
                           "exactly without it.");
            propertyName("Quality");
            choiceOption(state, *info, "##quality", "quality", qualityChoices, "normal");
        }
        propertyName("Filter");
        choiceOption(state, *info, "##filter", "filter", filterChoices, "linear");
        endProperties();
    }

    if (!highDynamicRange)
    {
        ImGui::SeparatorText("Sprites");
        if (beginProperties("sprites"))
        {
            propertyName("Mode");
            choiceOption(state, *info, "##sprite mode", "sprite_mode", spriteModeChoices, "none");
            if (mode != "none")
            {
                propertyName("Pixels per unit");
                auto pixelsPerUnit = static_cast<float>(numberOption(state, "pixels_per_unit", 100.0));
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragFloat("##ppu", &pixelsPerUnit, 0.5f, 0.01f, 100000.0f, "%g");
                ImGui::SetItemTooltip("How many pixels make a meter of the world");
                if (ImGui::IsItemDeactivatedAfterEdit() && pixelsPerUnit > 0.0f)
                {
                    setOption(state, *info, "pixels_per_unit", static_cast<double>(pixelsPerUnit));
                }
                if (mode == "grid")
                {
                    propertyName("Columns and rows");
                    std::array<int, 2> grid{static_cast<int>(columns), static_cast<int>(rows)};
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::DragInt2("##grid", grid.data(), 0.1f, 1, 1024);
                    ImGui::SetItemTooltip("How many cells across and down the image is cut into");
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        setOption(state, *info, "columns", static_cast<std::int64_t>(std::max(grid[0], 1)));
                        setOption(state, *info, "rows", static_cast<std::int64_t>(std::max(grid[1], 1)));
                    }
                }
                propertyName("Pivot");
                const auto preset = std::ranges::find_if(pivotChoices, [&](const PivotChoice& choice) {
                    return choice.pivot == math::Vec2{pivot.x, pivot.y};
                });
                if (beginCombo("##pivot preset", preset != pivotChoices.end() ? preset->label : "Custom"))
                {
                    for (const PivotChoice& choice : pivotChoices)
                    {
                        if (ImGui::Selectable(choice.label, preset != pivotChoices.end() && &choice == &*preset))
                        {
                            setOption(state, *info, "pivot", vectorValue(&choice.pivot.x, 2));
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip("Where the entity stands on each sprite: the point it turns around and is placed by");
                propertyName("");
                math::Vec2 custom{pivot.x, pivot.y};
                dragVector("##pivot", &custom[0], 2, 0.01f, "%.2f");
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    setOption(state, *info, "pivot", vectorValue(&custom.x, 2));
                }
                propertyName("Border");
                math::Vec4 border = vectorOption(state, "border", math::Vec4{0.0f});
                dragVector("##border", &border[0], 4, 0.2f, "%.0f");
                ImGui::SetItemTooltip("Pixels kept at their size by sliced and tiled sprites: left, bottom, right, top");
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    border = math::max(border, math::Vec4{0.0f});
                    setOption(state, *info, "border", vectorValue(&border.x, 4));
                }
            }
            endProperties();
        }
        if (mode != "none")
        {
            std::size_t sprites = 0;
            for (const asset::AssetId id : source->assets)
            {
                const asset::AssetInfo* const sprite = state.database->find(id);
                sprites += sprite != nullptr && sprite->type == asset::AssetType::Sprite ? 1 : 0;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, uiColor(colors.textDim));
            ImGui::TextWrapped("%zu %s, under the texture in the FileSystem: drag one into the view or onto a SpriteRenderer.",
                               sprites, sprites == 1 ? "sprite" : "sprites");
            ImGui::PopStyleColor();
            if (labelButton(icons::Clapperboard, "New Sprite Frames", 0.0f, sprites > 0))
            {
                const std::string folder = source->path.substr(0, source->path.find_last_of('/'));
                if (core::Result<std::filesystem::path> created = createSpriteFramesFile(state, folder, state.selectedAsset);
                    !created)
                {
                    DEVEX_LOG_ERROR("Cannot create the sprite frames: {}", created.error());
                }
            }
            ImGui::SetItemTooltip("Sprite frames beside the texture, with one animation of all its sprites");
            ImGui::SameLine();
            if (labelButton(icons::Grid, "New Tileset", 0.0f, sprites > 0))
            {
                const std::string folder = source->path.substr(0, source->path.find_last_of('/'));
                if (core::Result<std::filesystem::path> created = createTilesetFile(state, folder, state.selectedAsset);
                    !created)
                {
                    DEVEX_LOG_ERROR("Cannot create the tileset: {}", created.error());
                }
            }
            ImGui::SetItemTooltip("A tileset beside the texture, with a tile for each of its sprites");
        }
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Changing a setting imports the file again.");
}

} // namespace devex::tools::detail
