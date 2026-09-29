// The page of a model in the inspector: what its file brought, the way to place it, and how it
// imports, applied by Reimport.
#include "InspectorUi.hpp"

#include <devex/asset/import/Importer.hpp>
#include <devex/core/Log.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <string>

namespace devex::tools::detail {
namespace {

using scene::Entity;
using Button = PanelButton;

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

// Whether the importer of the file reads the option.
[[nodiscard]] bool hasOption(const asset::SourceFile& source, std::string_view key)
{
    const asset::Importer* const importer = asset::findImporter(source.importer);
    return importer != nullptr &&
           std::ranges::any_of(importer->defaultOptions, [&](const serialization::TextProperty& property) { return property.key == key; });
}

class ModelPage final : public InspectorPage
{
public:
    std::string signature(ToolsState& state) override
    {
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        return source ? std::format("{}|{}|{}", static_cast<int>(source->status), hasOption(*source, "scale"), source->error)
                      : std::string{};
    }

    void build(InspectorUi& ui, ToolsState& state, EditorUiKit& kit) override
    {
        m_settings.show(state.selectedAsset);
        const asset::AssetInfo* const info = state.database->find(state.selectedAsset);
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        if (info == nullptr || !source)
        {
            return;
        }
        ui.heading(kit, icons::Package, themeColors().entity, info->name, source->path);
        const Entity row = ui.actions(nullptr);
        m_place = ui.action(kit, row, Icon::Plus, "Place in Scene");
        ui.tooltip(m_place.entity, "Or drag the model into the viewport or the scene tree");
        ui.enable(m_place, source->status != asset::ImportStatus::Importing);
        if (source->status == asset::ImportStatus::Importing)
        {
            ui.note(nullptr, "Importing...");
        }
        else if (source->status == asset::ImportStatus::Failed)
        {
            ui.note(nullptr, "The file could not be imported.", "error");
            ui.note(nullptr, source->error, "dim", 3.0f);
        }

        // What the file brought, from its last successful import.
        Section& contents = ui.card(kit, "Contents");
        constexpr std::array<const char*, 4> names{"Meshes", "Materials", "Textures", "Animations"};
        for (std::size_t index = 0; index < names.size(); ++index)
        {
            const FormRow line = ui.formRow(contents, names[index]);
            m_counts[index] = ui.text(line.editor, rects::whole(math::Vec4{ui.font * 0.3f, 0.0f, 0.0f, 0.0f}), "", "text");
        }

        Section& import = ui.card(kit, "Import");
        m_scale = {};
        if (hasOption(*source, "scale"))
        {
            m_scaleRow = ui.formRow(import, "Scale");
            const std::array<std::string_view, 1> letters{""};
            m_scale = ui.numbers(m_scaleRow.editor, letters, {.minValue = 0.0001f, .maxValue = 100000.0f, .dragSpeed = 0.01f, .decimals = 4})
                          .front();
            ui.tooltip(m_scaleRow.editor, "Multiplies the size of the model, once converted to meters.\nModels already placed keep the "
                                          "positions of their nodes: place them again.");
        }
        m_compressRow = ui.formRow(import, "Compress textures");
        m_compress = ui.toggle(m_compressRow.editor);
        ui.tooltip(m_compressRow.editor, "BC7 and BC5 on the GPU: a quarter of the memory, slower to import");
        m_qualityRow = ui.formRow(import, "Texture quality");
        std::vector<std::string> labels;
        for (const QualityChoice& choice : qualityChoices)
        {
            labels.emplace_back(choice.label);
        }
        m_quality = ui.choice(m_qualityRow.editor, std::move(labels));
        m_footer = importFooter(ui, kit, import);
    }

    void sync(InspectorUi& ui, ToolsState& state, EditorUiKit& kit) override
    {
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        if (!source || !m_compress.isValid())
        {
            return;
        }
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
        for (std::size_t index = 0; index < counts.size(); ++index)
        {
            ui.scene().get<scene::UiText>(m_counts[index]).text = std::format("{}", counts[index]);
        }

        const ui::UiWorld& world = ui.panel.world();
        if (m_scale.isValid() && world.editedField() != m_scale && world.held() != m_scale)
        {
            ui.scene().get<scene::UiNumberField>(m_scale).value = static_cast<float>(m_settings.number(state, "scale", 1.0));
        }
        if (m_scale.isValid())
        {
            ui.scene().get<scene::UiRect>(m_scaleRow.mark).visible = m_settings.waits("scale");
        }
        ui.setToggle(m_compress, m_settings.boolean(state, "compress_textures", true));
        ui.scene().get<scene::UiRect>(m_compressRow.mark).visible = m_settings.waits("compress_textures");
        const std::string quality = m_settings.text(state, "texture_quality", "normal");
        const auto chosen = std::ranges::find_if(qualityChoices, [&](const QualityChoice& choice) { return quality == choice.option; });
        scene::UiDropdown& dropdown = ui.scene().get<scene::UiDropdown>(m_quality);
        dropdown.selected = chosen != qualityChoices.end() ? static_cast<std::int32_t>(chosen - qualityChoices.begin()) : -1;
        dropdown.placeholder = quality;
        ui.tooltip(m_quality, chosen != qualityChoices.end() ? chosen->tooltip : "");
        ui.scene().get<scene::UiRect>(m_qualityRow.mark).visible = m_settings.waits("texture_quality");
        syncImportFooter(ui, kit, m_footer, m_settings);
    }

    void answer(InspectorUi& ui, ToolsState& state, EditorUiKit&) override
    {
        if (!m_compress.isValid())
        {
            return;
        }
        const ui::UiWorld& world = ui.panel.world();
        if (world.wasClicked(m_place.entity))
        {
            requestInstantiateModel(state, state.selectedAsset, core::Uuid{});
        }
        if (m_scale.isValid() && world.wasChanged(m_scale))
        {
            const float scale = ui.scene().get<scene::UiNumberField>(m_scale).value;
            if (scale > 0.0f)
            {
                m_settings.set(state, "scale", static_cast<double>(scale));
            }
        }
        if (world.wasChanged(m_compress))
        {
            m_settings.set(state, "compress_textures", ui.scene().get<scene::UiToggle>(m_compress).value);
        }
        if (world.wasChanged(m_quality))
        {
            const std::int32_t selected = ui.scene().get<scene::UiDropdown>(m_quality).selected;
            if (selected >= 0 && static_cast<std::size_t>(selected) < qualityChoices.size())
            {
                m_settings.set(state, "texture_quality", std::string(qualityChoices[static_cast<std::size_t>(selected)].option));
            }
        }
        answerImportFooter(ui, state, m_footer, m_settings);
    }

private:
    ImportSettings m_settings;
    Button m_place;
    std::array<Entity, 4> m_counts{};
    FormRow m_scaleRow;
    Entity m_scale;
    FormRow m_compressRow;
    Entity m_compress;
    FormRow m_qualityRow;
    Entity m_quality;
    ImportFooter m_footer;
};

} // namespace

std::unique_ptr<InspectorPage> makeModelPage()
{
    return std::make_unique<ModelPage>();
}

} // namespace devex::tools::detail
