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

struct QualityChoice
{
    const char* option;
    const char* label;
    const char* tooltip;
};

constexpr std::array qualityChoices{
    QualityChoice{"fast", "Fast", "Quick to import, for a first look"},
    QualityChoice{"normal", "Normal", "The balance of import time and image quality"},
    QualityChoice{"high", "High", "Slow to import, for the final images"},
};

void setOption(ToolsState& state, const asset::AssetInfo& info, std::string_view key, serialization::TextValue value)
{
    if (core::Result<void> changed = state.database->setImportOption(state.selectedAsset, key, std::move(value)); !changed)
    {
        DEVEX_LOG_ERROR("Cannot change how {} imports: {}", info.name, changed.error());
    }
}

// Whether the importer of the file reads the option.
[[nodiscard]] bool hasOption(const asset::SourceFile& source, std::string_view key)
{
    const asset::Importer* const importer = asset::findImporter(source.importer);
    return importer != nullptr &&
           std::ranges::any_of(importer->defaultOptions, [&](const serialization::TextProperty& property) { return property.key == key; });
}

} // namespace

void drawModelInspector(ToolsState& state)
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

    ImGui::AlignTextToFramePadding();
    iconLabel(icons::Package, colors.entity);
    boldText(info->name.c_str());
    ImGui::TextDisabled("%s", source->path.c_str());
    ImGui::Spacing();

    if (labelButton(icons::Plus, "Place in Scene", 0.0f, source->status != asset::ImportStatus::Importing))
    {
        requestInstantiateModel(state, state.selectedAsset, core::Uuid{});
    }
    ImGui::SetItemTooltip("Or drag the model into the viewport or the hierarchy");
    ImGui::SameLine();
    if (labelButton(icons::Refresh, "Reimport"))
    {
        if (core::Result<void> queued = state.database->reimport(state.selectedAsset); !queued)
        {
            DEVEX_LOG_WARNING("{}", queued.error());
        }
    }
    ImGui::Spacing();

    if (source->status == asset::ImportStatus::Importing)
    {
        ImGui::TextDisabled("Importing...");
    }
    else if (source->status == asset::ImportStatus::Failed)
    {
        ImGui::TextColored(uiColor(colors.error), "The file could not be imported.");
        ImGui::TextWrapped("%s", source->error.c_str());
    }

    // What the file brought, from its last successful import.
    std::array<std::size_t, 4> counts{};
    for (const asset::AssetId id : source->assets)
    {
        if (const asset::AssetInfo* const asset = state.database->find(id))
        {
            counts[0] += asset->type == asset::AssetType::Mesh ? 1 : 0;
            counts[1] += asset->type == asset::AssetType::Material ? 1 : 0;
            counts[2] += asset->type == asset::AssetType::Texture ? 1 : 0;
            counts[3] += asset->type == asset::AssetType::AnimationClip ? 1 : 0;
        }
    }
    if (beginProperties("model"))
    {
        constexpr std::array<const char*, 4> names{"Meshes", "Materials", "Textures", "Animations"};
        for (std::size_t index = 0; index < names.size(); ++index)
        {
            propertyName(names[index]);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%zu", counts[index]);
        }
        endProperties();
    }

    ImGui::SeparatorText("Import");
    if (beginProperties("import"))
    {
        if (hasOption(*source, "scale"))
        {
            propertyName("Scale");
            const std::optional<serialization::TextValue> option = state.database->importOption(state.selectedAsset, "scale");
            auto scale = static_cast<float>(option ? serialization::asNumber(*option).value_or(1.0) : 1.0);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputFloat("##scale", &scale, 0.0f, 0.0f, "%g");
            ImGui::SetItemTooltip("Multiplies the size of the model, once converted to meters.\n"
                                  "Models already placed keep the positions of their nodes: place them again.");
            if (ImGui::IsItemDeactivatedAfterEdit() && scale > 0.0f)
            {
                setOption(state, *info, "scale", static_cast<double>(scale));
            }
        }

        propertyName("Compress textures");
        const std::optional<serialization::TextValue> compressOption =
            state.database->importOption(state.selectedAsset, "compress_textures");
        bool compress = compressOption ? serialization::asBool(*compressOption).value_or(true) : true;
        if (ImGui::Checkbox("##compress", &compress))
        {
            setOption(state, *info, "compress_textures", compress);
        }
        ImGui::SetItemTooltip("BC7 and BC5 on the GPU: a quarter of the memory, slower to import");

        propertyName("Texture quality");
        const std::optional<serialization::TextValue> qualityOption =
            state.database->importOption(state.selectedAsset, "texture_quality");
        const std::string* const text = qualityOption ? serialization::asString(*qualityOption) : nullptr;
        const std::string current = text != nullptr ? *text : "normal";
        const auto chosen = std::ranges::find_if(qualityChoices, [&](const QualityChoice& choice) { return current == choice.option; });
        if (beginCombo("##quality", chosen != qualityChoices.end() ? chosen->label : current.c_str()))
        {
            for (const QualityChoice& choice : qualityChoices)
            {
                if (ImGui::Selectable(choice.label, current == choice.option) && current != choice.option)
                {
                    setOption(state, *info, "texture_quality", std::string(choice.option));
                }
                ImGui::SetItemTooltip("%s", choice.tooltip);
            }
            ImGui::EndCombo();
        }
        endProperties();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Changing a setting imports the file again.");
}

} // namespace devex::tools::detail
