// The pages of shaders and materials in the inspector. A shader shows what it draws, its uniforms and
// the errors of its last import, with ways to edit it and to make a material of it. A material of a
// .dvxmat file chooses its shader, the standard one or one of the project, and edits the values it
// gives it: those of the standard surface, or the uniforms of the shader as their hints show them.
// Every change is written to the file and imported again, so that the scene follows at once.
#include "InspectorUi.hpp"

#include <devex/asset/Artifact.hpp>
#include <devex/asset/import/MaterialFile.hpp>
#include <devex/asset/import/ShaderFile.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace devex::tools::detail {
namespace {

using scene::Entity;
using Button = PanelButton;

[[nodiscard]] std::optional<asset::ShaderData> loadShader(const ToolsState& state, asset::AssetId id)
{
    if (state.database == nullptr || !id.isValid())
    {
        return std::nullopt;
    }
    const core::Result<std::vector<std::byte>> bytes = state.database->loadArtifact(id);
    if (!bytes)
    {
        return std::nullopt;
    }
    core::Result<asset::ShaderData> shader = asset::decodeShader(*bytes);
    return shader ? std::optional<asset::ShaderData>(std::move(*shader)) : std::nullopt;
}

[[nodiscard]] std::optional<std::filesystem::path> sourceFile(const ToolsState& state, asset::AssetId id)
{
    const std::optional<asset::SourceFile> source = state.database->sourceOf(id);
    return source ? state.database->project().absolutePath(source->path) : std::nullopt;
}

[[nodiscard]] std::string kindLabel(asset::ShaderKind kind)
{
    switch (kind)
    {
    case asset::ShaderKind::Spatial:
        return "Spatial: the surfaces of meshes";
    case asset::ShaderKind::CanvasItem:
        return "Canvas Item: sprites and tilemaps";
    case asset::ShaderKind::Particles:
        return "Particles: the particles of an emitter";
    case asset::ShaderKind::Sky:
        return "Sky: the sky and the light it gives";
    }
    return "Unknown";
}

[[nodiscard]] std::string uniformLabel(const asset::ShaderParameter& parameter)
{
    std::string text = std::format("{} {}", asset::toString(parameter.type), parameter.name);
    switch (parameter.hint)
    {
    case asset::ShaderHint::Range:
        text += std::format("  [{:g} to {:g}]", parameter.range.x, parameter.range.y);
        break;
    case asset::ShaderHint::Color:
        text += "  colour";
        break;
    case asset::ShaderHint::White:
        text += "  white when empty";
        break;
    case asset::ShaderHint::Black:
        text += "  black when empty";
        break;
    case asset::ShaderHint::Normal:
        text += "  flat normal when empty";
        break;
    case asset::ShaderHint::None:
        break;
    }
    return text;
}

// ---- A shader ----

class ShaderPage final : public InspectorPage
{
public:
    std::string signature(ToolsState& state) override
    {
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        const int status = source ? static_cast<int>(source->status) : -1;
        // Read again once an import ends.
        if (m_asset != state.selectedAsset || m_status != status)
        {
            m_asset = state.selectedAsset;
            m_status = status;
            m_shader = loadShader(state, state.selectedAsset);
            ++m_revision;
        }
        return std::format("{}|{}", status, m_revision);
    }

    void build(InspectorUi& ui, ToolsState& state, EditorUiKit& kit) override
    {
        const asset::AssetInfo* const info = state.database->find(state.selectedAsset);
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        if (info == nullptr || !source)
        {
            return;
        }
        const ThemeColors& colors = themeColors();
        ui.heading(kit, icons::FileCode, colors.material, info->name, source->path);
        const Entity row = ui.actions(nullptr);
        m_edit = ui.action(kit, row, Icon::Pencil, "Edit");
        m_material = ui.action(kit, row, Icon::Palette, "New Material");
        ui.tooltip(m_material.entity, "A material drawn by this shader, beside it, to give it values");
        if (!m_shader)
        {
            ui.note(nullptr, source->status == asset::ImportStatus::Importing ? "Importing..." : "The shader could not be read.",
                    source->status == asset::ImportStatus::Importing ? "dim" : "error");
            return;
        }
        const asset::ShaderData& shader = *m_shader;
        Section& card = ui.card(kit, "Shader");
        const auto fact = [&](const char* name, std::string value) {
            const FormRow line = ui.formRow(card, name);
            ui.text(line.editor, rects::whole(math::Vec4{ui.font * 0.3f, 0.0f, 0.0f, 0.0f}), std::move(value), "text");
        };
        fact("Type", kindLabel(shader.kind));
        if (shader.kind == asset::ShaderKind::Spatial)
        {
            fact("Faces", shader.cull == asset::ShaderCull::Back    ? "Front"
                          : shader.cull == asset::ShaderCull::Front ? "Back"
                                                                    : "Both");
            fact("Drawn", shader.transparent ? (shader.blend == asset::ShaderBlend::Add ? "Blended, adding its light" : "Blended by its alpha")
                                             : shader.discards ? "Opaque, with holes" : "Opaque");
            fact("Shadows", shader.castsShadows ? "Cast" : "None");
        }
        fact("State", shader.compiled() ? "Compiled" : "Not compiled: its materials draw as the default one");

        Section& uniforms = ui.card(kit, std::format("Uniforms ({})", shader.parameters.size()));
        if (shader.parameters.empty())
        {
            ui.note(&uniforms, "None: the materials of this shader give it no values.");
        }
        for (const asset::ShaderParameter& parameter : shader.parameters)
        {
            ui.note(&uniforms, uniformLabel(parameter), "text");
        }

        if (!shader.diagnostics.empty())
        {
            Section& errors = ui.card(kit, std::format("Errors and warnings ({})", shader.diagnostics.size()));
            for (const asset::ShaderDiagnostic& diagnostic : shader.diagnostics)
            {
                const std::string where = diagnostic.line > 0 ? std::format("Line {}: ", diagnostic.line) : std::string{};
                ui.note(&errors, where + diagnostic.message, diagnostic.error ? "error" : "warning", 2.0f);
            }
        }
        ui.note(nullptr, "Give a material this shader, then the material to a mesh, a sprite, a particle emitter or the "
                         "environment, as the type of the shader draws.",
                "dim", 3.0f);
    }

    void sync(InspectorUi& ui, ToolsState& state, EditorUiKit&) override
    {
        if (m_edit.entity.isValid())
        {
            ui.enable(m_edit, state.mode == ToolsMode::Editor);
            ui.enable(m_material, state.mode == ToolsMode::Editor && m_shader.has_value());
        }
    }

    void answer(InspectorUi& ui, ToolsState& state, EditorUiKit&) override
    {
        const ui::UiWorld& world = ui.panel.world();
        if (m_edit.entity.isValid() && world.wasClicked(m_edit.entity))
        {
            if (const std::optional<std::filesystem::path> file = sourceFile(state, state.selectedAsset))
            {
                openTextFile(state, *file);
            }
        }
        if (m_material.entity.isValid() && world.wasClicked(m_material.entity))
        {
            const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
            const asset::AssetInfo* const info = state.database->find(state.selectedAsset);
            if (source && info != nullptr)
            {
                const std::string folder = source->path.substr(0, source->path.rfind('/'));
                core::Result<std::filesystem::path> created =
                    createMaterialFile(state, folder, std::format("{} Material", info->name), state.selectedAsset);
                if (!created)
                {
                    DEVEX_LOG_WARNING("Cannot create the material: {}", created.error());
                }
            }
        }
    }

private:
    asset::AssetId m_asset;
    int m_status = -2;
    std::uint64_t m_revision = 0;
    std::optional<asset::ShaderData> m_shader;
    Button m_edit;
    Button m_material;
};

// ---- A material ----

// A value the page edits: of the standard surface, or a uniform of the shader.
struct Field
{
    std::string label;
    asset::ShaderParameterType type = asset::ShaderParameterType::Float;
    asset::ShaderHint hint = asset::ShaderHint::None;
    math::Vec3 range{0.0f, 1.0f, 0.0f};
    // A choice among names, whose index is the value.
    std::vector<std::string> options;
    std::string tooltip;
    std::function<math::Vec4(const asset::MaterialData&)> get;
    std::function<void(asset::MaterialData&, math::Vec4)> set;
    std::function<asset::AssetId(const asset::MaterialData&)> texture;
    std::function<void(asset::MaterialData&, asset::AssetId)> setTexture;
};

struct FieldRow
{
    FormRow row;
    std::vector<Entity> numbers;
    // The toggle, the choice, or the swatch.
    Entity control;
    std::vector<asset::AssetId> textures;
};

[[nodiscard]] Field number(std::string label, float asset::MaterialData::*member, float low, float high, std::string tooltip)
{
    return Field{
        .label = std::move(label),
        .hint = asset::ShaderHint::Range,
        .range = {low, high, 0.0f},
        .tooltip = std::move(tooltip),
        .get = [member](const asset::MaterialData& material) { return math::Vec4{material.*member, 0.0f, 0.0f, 0.0f}; },
        .set = [member](asset::MaterialData& material, math::Vec4 value) { material.*member = value.x; },
    };
}

[[nodiscard]] Field texture(std::string label, asset::AssetId asset::MaterialData::*member, asset::ShaderHint hint, std::string tooltip)
{
    return Field{
        .label = std::move(label),
        .type = asset::ShaderParameterType::Texture,
        .hint = hint,
        .tooltip = std::move(tooltip),
        .texture = [member](const asset::MaterialData& material) { return material.*member; },
        .setTexture = [member](asset::MaterialData& material, asset::AssetId id) { material.*member = id; },
    };
}

// The values of the standard surface, as the parameters of glTF name them.
[[nodiscard]] std::vector<Field> standardFields()
{
    std::vector<Field> fields;
    fields.push_back({
        .label = "Base Color",
        .type = asset::ShaderParameterType::Float4,
        .hint = asset::ShaderHint::Color,
        .tooltip = "The colour of the surface, and its opacity",
        .get = [](const asset::MaterialData& material) { return material.baseColorFactor; },
        .set = [](asset::MaterialData& material, math::Vec4 value) { material.baseColorFactor = value; },
    });
    fields.push_back(texture("Base Color Texture", &asset::MaterialData::baseColorTexture, asset::ShaderHint::White,
                             "Multiplies the base colour"));
    fields.push_back(number("Metallic", &asset::MaterialData::metallicFactor, 0.0f, 1.0f, "0 for a dielectric, 1 for a metal"));
    fields.push_back(number("Roughness", &asset::MaterialData::roughnessFactor, 0.0f, 1.0f, "0 for a mirror, 1 for chalk"));
    fields.push_back(texture("Metal & Rough Texture", &asset::MaterialData::metallicRoughnessTexture, asset::ShaderHint::White,
                             "Roughness in green, metalness in blue, multiplying them"));
    fields.push_back(texture("Normal Map", &asset::MaterialData::normalTexture, asset::ShaderHint::Normal,
                             "The relief of the surface, in tangent space"));
    fields.push_back(number("Normal Scale", &asset::MaterialData::normalScale, 0.0f, 2.0f, "How deep the relief looks"));
    fields.push_back(texture("Occlusion Texture", &asset::MaterialData::occlusionTexture, asset::ShaderHint::White,
                             "The corners the light reaches less, in red"));
    fields.push_back(number("Occlusion Strength", &asset::MaterialData::occlusionStrength, 0.0f, 1.0f, "How much it darkens"));
    fields.push_back({
        .label = "Emission",
        .type = asset::ShaderParameterType::Float3,
        .hint = asset::ShaderHint::Color,
        .tooltip = "The light the surface gives on its own, relative to the exposure",
        .get = [](const asset::MaterialData& material) { return math::Vec4(material.emissiveFactor, 1.0f); },
        .set = [](asset::MaterialData& material, math::Vec4 value) { material.emissiveFactor = math::Vec3(value); },
    });
    fields.push_back(texture("Emission Texture", &asset::MaterialData::emissiveTexture, asset::ShaderHint::White,
                             "Multiplies the emission"));
    fields.push_back({
        .label = "Transparency",
        .type = asset::ShaderParameterType::Int,
        .options = {"Opaque", "Cut Out", "Blended"},
        .tooltip = "Opaque; cut out below the alpha cutoff; or blended with what is behind",
        .get = [](const asset::MaterialData& material) { return math::Vec4{static_cast<float>(material.alphaMode), 0.0f, 0.0f, 0.0f}; },
        .set = [](asset::MaterialData& material, math::Vec4 value) { material.alphaMode = static_cast<asset::AlphaMode>(value.x); },
    });
    fields.push_back(number("Alpha Cutoff", &asset::MaterialData::alphaCutoff, 0.0f, 1.0f, "Cut out: below it, nothing is drawn"));
    fields.push_back({
        .label = "Double Sided",
        .type = asset::ShaderParameterType::Bool,
        .tooltip = "Draws the back faces too, lit as seen from their side",
        .get = [](const asset::MaterialData& material) { return math::Vec4{material.doubleSided ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f}; },
        .set = [](asset::MaterialData& material, math::Vec4 value) { material.doubleSided = value.x != 0.0f; },
    });
    return fields;
}

// The uniforms of a shader, as the material gives them: by name, or the default of the shader.
[[nodiscard]] std::vector<Field> uniformFields(const asset::ShaderData& shader)
{
    std::vector<Field> fields;
    for (const asset::ShaderParameter& parameter : shader.parameters)
    {
        Field field{
            .label = parameter.name,
            .type = parameter.type,
            .hint = parameter.hint,
            .range = parameter.range,
            .tooltip = uniformLabel(parameter),
        };
        const std::string name = parameter.name;
        const math::Vec4 fallback = parameter.defaultValue;
        const auto components = static_cast<std::uint8_t>(asset::componentCount(parameter.type));
        if (parameter.type == asset::ShaderParameterType::Texture)
        {
            field.texture = [name](const asset::MaterialData& material) {
                const asset::MaterialParameter* const given = material.findParameter(name);
                return given != nullptr ? given->texture : asset::AssetId{};
            };
            field.setTexture = [name](asset::MaterialData& material, asset::AssetId id) {
                std::erase_if(material.parameters, [&](const asset::MaterialParameter& given) { return given.name == name; });
                if (id.isValid())
                {
                    material.parameters.push_back({.name = name, .texture = id});
                }
            };
        }
        else
        {
            field.get = [name, fallback](const asset::MaterialData& material) {
                const asset::MaterialParameter* const given = material.findParameter(name);
                return given != nullptr && !given->texture.isValid() ? given->value : fallback;
            };
            field.set = [name, components](asset::MaterialData& material, math::Vec4 value) {
                auto found = std::ranges::find(material.parameters, name, &asset::MaterialParameter::name);
                if (found == material.parameters.end())
                {
                    found = material.parameters.insert(material.parameters.end(), {.name = name});
                }
                found->value = value;
                found->components = components;
                found->texture = {};
            };
        }
        fields.push_back(std::move(field));
    }
    return fields;
}

class MaterialPage final : public InspectorPage
{
public:
    std::string signature(ToolsState& state) override
    {
        refresh(state);
        std::string layout;
        for (const Field& field : m_fields)
        {
            layout += std::format("{}:{}:{};", field.label, static_cast<int>(field.type), static_cast<int>(field.hint));
        }
        return std::format("{}|{}|{}|{}", m_editable, m_material.shader.uuid.toString(), m_shaderRevision, layout);
    }

    void build(InspectorUi& ui, ToolsState& state, EditorUiKit& kit) override
    {
        m_rows.clear();
        m_shaderChoice = {};
        m_editShader = {};
        const asset::AssetInfo* const info = state.database->find(state.selectedAsset);
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        if (info == nullptr || !source)
        {
            return;
        }
        ui.heading(kit, icons::Palette, themeColors().material, info->name, source->path);
        if (!m_editable)
        {
            ui.note(nullptr, "This material comes from a model: it changes with the model. A .dvxmat material can be "
                             "edited here, or given in place of it to a MeshRenderer.",
                    "dim", 3.0f);
            return;
        }
        Section& shaderCard = ui.card(kit, "Shader");
        const FormRow shaderRow = ui.formRow(shaderCard, "Shader");
        m_shaderChoice = ui.choice(shaderRow.editor);
        ui.tooltip(shaderRow.editor, "The standard surface, or a shader of the project that draws the material");
        if (m_material.shader.isValid())
        {
            const Entity row = ui.actions(&shaderCard);
            m_editShader = ui.action(kit, row, Icon::Pencil, "Edit Shader");
            if (!m_shader)
            {
                ui.note(&shaderCard, "The shader is not imported yet, or could not be read.", "warning", 2.0f);
            }
            else if (!m_shader->compiled())
            {
                ui.note(&shaderCard, "The shader does not compile: the material draws as the default one until it does.", "error", 2.0f);
            }
        }

        Section& card = ui.card(kit, m_material.shader.isValid() ? "Uniforms" : "Surface");
        if (m_fields.empty())
        {
            ui.note(&card, "The shader has no uniforms.");
        }
        static constexpr std::array<std::string_view, 4> letters{"X", "Y", "Z", "W"};
        static constexpr std::array<std::string_view, 1> one{""};
        for (const Field& field : m_fields)
        {
            FieldRow& made = m_rows.emplace_back();
            made.row = ui.formRow(card, field.label);
            if (!field.tooltip.empty())
            {
                ui.tooltip(made.row.editor, field.tooltip);
            }
            const std::uint32_t count = asset::componentCount(field.type);
            if (field.type == asset::ShaderParameterType::Texture || !field.options.empty())
            {
                made.control = ui.choice(made.row.editor, field.options);
            }
            else if (field.type == asset::ShaderParameterType::Bool)
            {
                made.control = ui.toggle(made.row.editor);
            }
            else if (field.hint == asset::ShaderHint::Color && count >= 3)
            {
                made.control = ui.swatch(kit, made.row.editor, count == 4);
            }
            else
            {
                scene::UiNumberField settings{.dragSpeed = 0.01f, .decimals = 3};
                if (field.hint == asset::ShaderHint::Range && field.range.y > field.range.x)
                {
                    settings.minValue = field.range.x;
                    settings.maxValue = field.range.y;
                    settings.step = field.range.z;
                    settings.dragSpeed = (field.range.y - field.range.x) / 200.0f;
                }
                if (field.type == asset::ShaderParameterType::Int)
                {
                    settings.step = 1.0f;
                    settings.dragSpeed = 0.1f;
                    settings.decimals = 0;
                }
                made.numbers = ui.numbers(made.row.editor,
                                          count == 1 ? std::span<const std::string_view>(one)
                                                     : std::span<const std::string_view>(letters).first(count),
                                          settings);
            }
        }
    }

    void sync(InspectorUi& ui, ToolsState& state, EditorUiKit&) override
    {
        if (!m_shaderChoice.isValid())
        {
            return;
        }
        // The standard surface, then the shaders of the project.
        m_shaderOptions.assign(1, asset::AssetId{});
        std::vector<std::string> labels{"Standard"};
        std::int32_t selected = 0;
        for (const asset::AssetInfo& info : state.database->assets(asset::AssetType::Shader))
        {
            if (info.id == m_material.shader)
            {
                selected = static_cast<std::int32_t>(labels.size());
            }
            m_shaderOptions.push_back(info.id);
            labels.push_back(info.name);
        }
        ui.setChoice(m_shaderChoice, std::move(labels), selected);
        if (m_editShader.entity.isValid())
        {
            ui.enable(m_editShader, state.mode == ToolsMode::Editor);
        }

        for (std::size_t index = 0; index < m_rows.size() && index < m_fields.size(); ++index)
        {
            const Field& field = m_fields[index];
            FieldRow& row = m_rows[index];
            if (field.type == asset::ShaderParameterType::Texture)
            {
                fillTextures(ui, state, row, field.texture(m_material));
                continue;
            }
            const math::Vec4 value = field.get(m_material);
            if (!field.options.empty())
            {
                ui.setChoice(row.control, field.options, static_cast<std::int32_t>(value.x));
            }
            else if (field.type == asset::ShaderParameterType::Bool)
            {
                ui.setToggle(row.control, value.x != 0.0f);
            }
            else if (row.control.isValid())
            {
                ui.setSwatch(row.control, value, asset::componentCount(field.type) == 4);
            }
            for (std::size_t component = 0; component < row.numbers.size(); ++component)
            {
                ui.setNumber(row.numbers[component], value[static_cast<int>(component)]);
            }
        }
        if (m_colorField && *m_colorField < m_fields.size() && ui.colorPopupOpen())
        {
            const Field& field = m_fields[*m_colorField];
            ui.syncColorPopup(field.get(m_material), asset::componentCount(field.type) == 4);
        }
    }

    void answer(InspectorUi& ui, ToolsState& state, EditorUiKit&) override
    {
        const ui::UiWorld& world = ui.panel.world();
        bool dirty = false;
        if (m_shaderChoice.isValid() && world.wasChanged(m_shaderChoice))
        {
            const std::int32_t index = ui.scene().get<scene::UiDropdown>(m_shaderChoice).selected;
            if (index >= 0 && static_cast<std::size_t>(index) < m_shaderOptions.size() &&
                m_shaderOptions[static_cast<std::size_t>(index)] != m_material.shader)
            {
                m_material.shader = m_shaderOptions[static_cast<std::size_t>(index)];
                dirty = true;
            }
        }
        if (m_editShader.entity.isValid() && world.wasClicked(m_editShader.entity))
        {
            if (const std::optional<std::filesystem::path> file = sourceFile(state, m_material.shader))
            {
                openTextFile(state, *file);
            }
        }
        for (std::size_t index = 0; index < m_rows.size() && index < m_fields.size(); ++index)
        {
            const Field& field = m_fields[index];
            const FieldRow& row = m_rows[index];
            if (field.type == asset::ShaderParameterType::Texture)
            {
                if (world.wasChanged(row.control))
                {
                    const std::int32_t chosen = ui.scene().get<scene::UiDropdown>(row.control).selected;
                    if (chosen >= 0 && static_cast<std::size_t>(chosen) < row.textures.size())
                    {
                        field.setTexture(m_material, row.textures[static_cast<std::size_t>(chosen)]);
                        dirty = true;
                    }
                }
                continue;
            }
            math::Vec4 value = field.get(m_material);
            if (!field.options.empty() && world.wasChanged(row.control))
            {
                value.x = static_cast<float>(std::max(ui.scene().get<scene::UiDropdown>(row.control).selected, 0));
                field.set(m_material, value);
                dirty = true;
            }
            else if (field.type == asset::ShaderParameterType::Bool && world.wasChanged(row.control))
            {
                value.x = ui.scene().get<scene::UiToggle>(row.control).value ? 1.0f : 0.0f;
                field.set(m_material, value);
                dirty = true;
            }
            else if (field.options.empty() && field.type != asset::ShaderParameterType::Bool && row.control.isValid() &&
                     world.wasClicked(row.control))
            {
                m_colorField = index;
                ui.openColorPopup(row.control);
            }
            for (std::size_t component = 0; component < row.numbers.size(); ++component)
            {
                if (world.wasChanged(row.numbers[component]))
                {
                    value[static_cast<int>(component)] = ui.scene().get<scene::UiNumberField>(row.numbers[component]).value;
                    field.set(m_material, value);
                    dirty = true;
                }
            }
        }
        if (m_colorField && *m_colorField < m_fields.size())
        {
            const Field& field = m_fields[*m_colorField];
            const math::Vec4 current = field.get(m_material);
            if (const std::optional<math::Vec4> chosen = ui.answerColorPopup(current))
            {
                field.set(m_material, asset::componentCount(field.type) == 4 ? *chosen : math::Vec4(math::Vec3(*chosen), 1.0f));
                dirty = true;
            }
            if (!ui.colorPopupOpen())
            {
                m_colorField.reset();
            }
        }
        if (dirty)
        {
            save(state);
        }
    }

private:
    // Reads the file when another material is selected, when it changed on disk, and the uniforms of
    // its shader when that one imports again.
    void refresh(ToolsState& state)
    {
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        const std::optional<std::filesystem::path> file =
            source && source->path.ends_with(asset::materialExtension) ? state.database->project().absolutePath(source->path)
                                                                       : std::nullopt;
        std::error_code error;
        const std::filesystem::file_time_type time =
            file ? std::filesystem::last_write_time(*file, error) : std::filesystem::file_time_type{};
        const bool reread = m_asset != state.selectedAsset || m_file != file || m_fileTime != time;
        if (reread)
        {
            m_asset = state.selectedAsset;
            m_file = file;
            m_fileTime = time;
            m_editable = false;
            m_material = {};
            if (file)
            {
                const core::Result<std::string> text = core::readTextFile(*file);
                core::Result<asset::MaterialData> material =
                    text ? asset::parseMaterialFile(*text) : core::Result<asset::MaterialData>(std::unexpected(text.error()));
                if (material)
                {
                    m_material = std::move(*material);
                    m_editable = true;
                }
            }
            m_colorField.reset();
        }
        const std::optional<asset::SourceFile> shaderSource =
            m_material.shader.isValid() ? state.database->sourceOf(m_material.shader) : std::nullopt;
        const int shaderStatus = shaderSource ? static_cast<int>(shaderSource->status) : -1;
        if (reread || m_shaderId != m_material.shader || m_shaderStatus != shaderStatus)
        {
            m_shaderId = m_material.shader;
            m_shaderStatus = shaderStatus;
            m_shader = loadShader(state, m_material.shader);
            ++m_shaderRevision;
            m_fields = !m_material.shader.isValid() ? standardFields()
                       : m_shader                   ? uniformFields(*m_shader)
                                                    : std::vector<Field>{};
        }
    }

    // A list of textures to choose from, with nothing first: all of them while it may open, the one
    // chosen otherwise.
    static void fillTextures(InspectorUi& ui, const ToolsState& state, FieldRow& row, asset::AssetId value)
    {
        const ui::UiWorld& world = ui.panel.world();
        const bool full = world.hovered() == row.control || world.focused() == row.control || world.listedDropdown() == row.control;
        row.textures.assign(1, asset::AssetId{});
        if (full)
        {
            for (const asset::AssetInfo& info : state.database->assets(asset::AssetType::Texture))
            {
                row.textures.push_back(info.id);
            }
        }
        else if (value.isValid())
        {
            row.textures.push_back(value);
        }
        std::vector<std::string> options;
        std::int32_t selected = -1;
        for (const asset::AssetId texture : row.textures)
        {
            if (texture == value)
            {
                selected = static_cast<std::int32_t>(options.size());
            }
            options.push_back(assetLabel(state, texture));
        }
        ui.setChoice(row.control, std::move(options), selected);
    }

    void save(ToolsState& state)
    {
        if (!m_file)
        {
            return;
        }
        if (core::Result<void> written = core::writeTextFile(*m_file, asset::writeMaterialFile(m_material)); !written)
        {
            DEVEX_LOG_ERROR("Cannot save the material: {}", written.error());
            return;
        }
        std::error_code error;
        m_fileTime = std::filesystem::last_write_time(*m_file, error);
        if (core::Result<void> queued = state.database->reimport(m_asset); !queued)
        {
            DEVEX_LOG_WARNING("{}", queued.error());
        }
    }

    asset::AssetId m_asset;
    std::optional<std::filesystem::path> m_file;
    std::filesystem::file_time_type m_fileTime{};
    bool m_editable = false;
    asset::MaterialData m_material;
    asset::AssetId m_shaderId;
    int m_shaderStatus = -2;
    std::uint64_t m_shaderRevision = 0;
    std::optional<asset::ShaderData> m_shader;
    std::vector<Field> m_fields;
    std::vector<FieldRow> m_rows;
    Entity m_shaderChoice;
    std::vector<asset::AssetId> m_shaderOptions;
    Button m_editShader;
    std::optional<std::size_t> m_colorField;
};

} // namespace

std::unique_ptr<InspectorPage> makeShaderPage()
{
    return std::make_unique<ShaderPage>();
}

std::unique_ptr<InspectorPage> makeMaterialPage()
{
    return std::make_unique<MaterialPage>();
}

const std::vector<CodeDiagnostic>& shaderDiagnosticsOf(ToolsState& state, const std::filesystem::path& file)
{
    static const std::vector<CodeDiagnostic> none;
    if (state.database == nullptr || file.extension() != asset::shaderExtension)
    {
        return none;
    }
    const std::string key = core::toUtf8(file);
    if (const auto found = state.shaderDiagnostics.find(key); found != state.shaderDiagnostics.end())
    {
        return found->second;
    }
    std::vector<CodeDiagnostic> diagnostics;
    if (const std::optional<asset::AssetId> id = state.database->findByPath(state.database->project().resourcePath(file)))
    {
        if (const std::optional<asset::ShaderData> shader = loadShader(state, *id))
        {
            for (const asset::ShaderDiagnostic& diagnostic : shader->diagnostics)
            {
                // Those of the shader as a whole go on its first line.
                diagnostics.push_back({.path = file,
                                       .line = std::max(static_cast<int>(diagnostic.line), 1),
                                       .column = std::max(static_cast<int>(diagnostic.column), 1),
                                       .message = diagnostic.message,
                                       .error = diagnostic.error});
            }
        }
    }
    return state.shaderDiagnostics.emplace(key, std::move(diagnostics)).first->second;
}

core::Result<std::filesystem::path> createShaderFile(ToolsState& state, std::string_view folder, std::string_view name,
                                                     asset::ShaderKind kind)
{
    return writeNewAssetFile(state, folder, name, "Shader", asset::shaderExtension, asset::shaderTemplate(kind));
}

core::Result<std::filesystem::path> createMaterialFile(ToolsState& state, std::string_view folder, std::string_view name,
                                                       asset::AssetId shader)
{
    asset::MaterialData material;
    material.shader = shader;
    return writeNewAssetFile(state, folder, name, "Material", asset::materialExtension, asset::writeMaterialFile(material));
}

} // namespace devex::tools::detail
