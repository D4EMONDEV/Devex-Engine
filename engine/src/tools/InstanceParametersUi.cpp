// The Instance Shader Parameters of the renderers in the Inspector: the instance uniforms of the
// shader each draws with, the value the entity gives them, and a way back to the default, as Godot
// shows them under its GeometryInstance3D.
#include "InspectorUi.hpp"

#include <devex/asset/Artifact.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/scene/InstanceShaderParameters.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <iterator>
#include <optional>
#include <utility>

namespace devex::tools::detail {

using scene::Entity;
using scene::UiRect;
using namespace rects;

namespace {

// The material a renderer draws with, read by its reflection.
[[nodiscard]] asset::AssetId materialOf(const scene::ComponentType& type, const void* component)
{
    const reflection::FieldInfo* const field = type.type->findField("material");
    return field != nullptr && field->kind == reflection::ValueKind::AssetId && field->list == nullptr
               ? *static_cast<const asset::AssetId*>(field->address(component))
               : asset::AssetId{};
}

[[nodiscard]] scene::InstanceShaderValues valuesOf(const scene::ComponentType& type, const scene::Scene& edited, Entity entity)
{
    // Only written through the scene the inspector edits, which gives it as const for reading.
    return scene::instanceShaderValues(type, const_cast<void*>(type.find(edited, entity)));
}

} // namespace

const std::vector<asset::ShaderParameter>& InspectorUi::instanceUniformsOf(const ToolsState& state, asset::AssetId material)
{
    if (instanceUniformsRevision != state.materialRevision)
    {
        instanceUniforms.clear();
        instanceUniformsRevision = state.materialRevision;
    }
    if (const auto found = instanceUniforms.find(material); found != instanceUniforms.end())
    {
        return found->second;
    }
    std::vector<asset::ShaderParameter> uniforms;
    const auto shaderOf = [&]() -> std::optional<asset::ShaderData> {
        if (state.database == nullptr || !material.isValid())
        {
            return std::nullopt;
        }
        const core::Result<std::vector<std::byte>> bytes = state.database->loadArtifact(material);
        const core::Result<asset::MaterialData> data =
            bytes ? asset::decodeMaterial(*bytes) : core::Result<asset::MaterialData>(std::unexpected(bytes.error()));
        if (!data || !data->shader.isValid())
        {
            return std::nullopt;
        }
        const core::Result<std::vector<std::byte>> shaderBytes = state.database->loadArtifact(data->shader);
        core::Result<asset::ShaderData> shader =
            shaderBytes ? asset::decodeShader(*shaderBytes) : core::Result<asset::ShaderData>(std::unexpected(shaderBytes.error()));
        return shader ? std::optional(std::move(*shader)) : std::nullopt;
    };
    if (const std::optional<asset::ShaderData> shader = shaderOf())
    {
        std::ranges::copy_if(shader->parameters, std::back_inserter(uniforms), [](const asset::ShaderParameter& uniform) { return uniform.instance; });
    }
    return instanceUniforms.emplace(material, std::move(uniforms)).first->second;
}

std::string InspectorUi::instanceSignature(const ToolsState& state, const scene::Scene& edited, std::span<const Entity> inspected)
{
    if (inspected.size() != 1)
    {
        return {};
    }
    std::string text;
    for (const scene::ComponentType& type : scene::componentRegistry().types())
    {
        const void* const component = type.find(edited, inspected.front());
        if (component == nullptr || !valuesOf(type, edited, inspected.front()).isValid())
        {
            continue;
        }
        const asset::AssetId material = materialOf(type, component);
        text += std::format("|{}:{}", type.name(), material.uuid.toString());
        for (const asset::ShaderParameter& uniform : instanceUniformsOf(state, material))
        {
            text += std::format(",{}{}{}", uniform.name, static_cast<int>(uniform.type), static_cast<int>(uniform.hint));
        }
    }
    return text;
}

void InspectorUi::addInstanceParameters(EditorUiKit& kit, ToolsState& state, Section& section, const scene::ComponentType& type,
                                        const scene::Scene& edited, Entity active)
{
    const void* const component = type.find(edited, active);
    if (component == nullptr || !valuesOf(type, edited, active).isValid())
    {
        return;
    }
    const std::vector<asset::ShaderParameter>& uniforms = instanceUniformsOf(state, materialOf(type, component));
    if (uniforms.empty())
    {
        return;
    }
    // A group of the card, folded from its header as those of the fields are.
    section.groups.emplace_back("Instance Shader Parameters");
    const int group = static_cast<int>(section.groups.size()) - 1;
    const Entity heading = add(section.card, "Group", wide(std::round(line * 0.95f)), "group");
    scene().add<scene::UiImage>(heading);
    scene().add<scene::UiButton>(heading);
    scene().add<scene::UiFoldout>(heading);
    text(heading, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {std::min(line, 28.0f), 0.0f}, .offsetMax = {0.0f, 0.0f}},
         "Instance Shader Parameters", "dim", true);
    tooltip(heading, "The instance uniforms of the shader of the material: this object gives them its own values, and "
                     "the others drawn with the material keep theirs");
    section.lines.push_back(Line{.entity = heading, .group = group, .heading = true});

    static constexpr std::array<std::string_view, 4> letters{"X", "Y", "Z", "W"};
    static constexpr std::array<std::string_view, 1> one{""};
    const float revertSize = std::round(line * 0.8f);
    for (const asset::ShaderParameter& uniform : uniforms)
    {
        InstanceRow& made = instanceRows.emplace_back();
        made.type = &type;
        made.uniform = uniform;
        made.form = formRow(section, uniform.name);
        section.lines.back().group = group;
        tooltip(made.form.label, std::format("instance uniform {} {}", asset::toString(uniform.type), uniform.name));
        // Room for the button that takes the default again, at the end of the row.
        scene().get<UiRect>(made.form.editor).offsetMax.x = -(revertSize + 4.0f);
        made.revert = toolButton(kit, made.form.row, Icon::Undo,
                                 UiRect{.anchorMin = {1.0f, 0.5f}, .anchorMax = {1.0f, 0.5f}, .offsetMin = {-revertSize - 2.0f, -revertSize * 0.5f},
                                        .offsetMax = {-2.0f, revertSize * 0.5f}});
        tooltip(made.revert.entity, "Takes the default of the shader again");

        const std::uint32_t count = asset::componentCount(uniform.type);
        if (uniform.type == asset::ShaderParameterType::Bool)
        {
            made.control = toggle(made.form.editor);
        }
        else if (uniform.hint == asset::ShaderHint::Color && count >= 3)
        {
            made.control = swatch(kit, made.form.editor, count == 4);
        }
        else
        {
            scene::UiNumberField settings{.dragSpeed = 0.01f, .decimals = 3};
            if (uniform.hint == asset::ShaderHint::Range && uniform.range.y > uniform.range.x)
            {
                settings.minValue = uniform.range.x;
                settings.maxValue = uniform.range.y;
                settings.step = uniform.range.z;
                settings.dragSpeed = (uniform.range.y - uniform.range.x) / 200.0f;
            }
            if (uniform.type == asset::ShaderParameterType::Int)
            {
                settings.step = 1.0f;
                settings.dragSpeed = 0.1f;
                settings.decimals = 0;
            }
            made.numbers = numbers(made.form.editor,
                                   count == 1 ? std::span<const std::string_view>(one) : std::span<const std::string_view>(letters).first(count),
                                   settings);
        }
    }
}

void InspectorUi::syncInstanceRows(const scene::Scene& edited, Entity active)
{
    const ui::UiWorld& world = panel.world();
    for (InstanceRow& row : instanceRows)
    {
        const scene::InstanceShaderValues values = valuesOf(*row.type, edited, active);
        const std::optional<math::Vec4> given = values.find(row.uniform.name);
        const math::Vec4 value = given.value_or(row.uniform.defaultValue);
        scene().get<UiRect>(row.form.mark).visible = given.has_value();
        scene().get<UiRect>(row.revert.entity).visible = given.has_value();
        if (row.uniform.type == asset::ShaderParameterType::Bool)
        {
            setToggle(row.control, value.x != 0.0f);
        }
        else if (row.control.isValid())
        {
            setSwatch(row.control, value, asset::componentCount(row.uniform.type) == 4);
        }
        for (std::size_t component = 0; component < row.numbers.size(); ++component)
        {
            // A number being typed into keeps what is typed.
            if (world.editedField() != row.numbers[component])
            {
                setNumber(row.numbers[component], value[static_cast<int>(component)]);
            }
        }
    }
    if (instanceColorRow && *instanceColorRow < instanceRows.size() && colorPopupOpen())
    {
        const InstanceRow& row = instanceRows[*instanceColorRow];
        const std::optional<math::Vec4> given = valuesOf(*row.type, edited, active).find(row.uniform.name);
        syncColorPopup(given.value_or(row.uniform.defaultValue), asset::componentCount(row.uniform.type) == 4);
    }
}

void InspectorUi::answerInstanceRows(ToolsState& state, scene::Scene& edited, Entity active)
{
    const ui::UiWorld& world = panel.world();
    // Begins one step of the history before the first change of an edit.
    const auto begin = [&](const InstanceRow& row) {
        if (instanceEdit && instanceEdit->type == row.type)
        {
            return;
        }
        commitInstanceEdit(state, edited);
        const void* const component = row.type->find(edited, active);
        const reflection::FieldInfo* const names = row.type->type->findField(scene::instanceShaderParametersField);
        const reflection::FieldInfo* const values = row.type->type->findField(scene::instanceShaderValuesField);
        instanceEdit = InstanceEdit{
            .type = row.type,
            .entity = edited.uuid(active),
            .names = scene::writeFieldValue(*names, names->address(component)),
            .values = scene::writeFieldValue(*values, values->address(component)),
        };
    };
    bool ended = false;
    for (std::size_t index = 0; index < instanceRows.size(); ++index)
    {
        const InstanceRow& row = instanceRows[index];
        const scene::InstanceShaderValues values = scene::instanceShaderValues(*row.type, row.type->findMutable(edited, active));
        if (!values.isValid())
        {
            continue;
        }
        math::Vec4 value = values.find(row.uniform.name).value_or(row.uniform.defaultValue);
        if (world.wasClicked(row.revert.entity))
        {
            begin(row);
            values.erase(row.uniform.name);
            ended = true;
            continue;
        }
        if (row.uniform.type == asset::ShaderParameterType::Bool && world.wasChanged(row.control))
        {
            begin(row);
            value.x = scene().get<scene::UiToggle>(row.control).value ? 1.0f : 0.0f;
            values.set(row.uniform.name, value);
            ended = true;
        }
        else if (row.uniform.type != asset::ShaderParameterType::Bool && row.control.isValid() && world.wasClicked(row.control))
        {
            instanceColorRow = index;
            openColorPopup(row.control);
        }
        for (std::size_t component = 0; component < row.numbers.size(); ++component)
        {
            if (world.wasChanged(row.numbers[component]))
            {
                begin(row);
                value[static_cast<int>(component)] = scene().get<scene::UiNumberField>(row.numbers[component]).value;
                values.set(row.uniform.name, value);
            }
        }
    }
    if (instanceColorRow && *instanceColorRow < instanceRows.size())
    {
        const InstanceRow& row = instanceRows[*instanceColorRow];
        const scene::InstanceShaderValues values = scene::instanceShaderValues(*row.type, row.type->findMutable(edited, active));
        const math::Vec4 current = values.find(row.uniform.name).value_or(row.uniform.defaultValue);
        if (const std::optional<math::Vec4> chosen = answerColorPopup(current); chosen && values.isValid())
        {
            begin(row);
            values.set(row.uniform.name, asset::componentCount(row.uniform.type) == 4 ? *chosen : math::Vec4(math::Vec3(*chosen), 1.0f));
        }
        if (!colorPopupOpen())
        {
            instanceColorRow.reset();
        }
    }

    // The step ends once nothing holds a number, nothing is typed and the colour is chosen.
    const auto busy = [&](Entity entity) { return entity.isValid() && (entity == world.held() || entity == world.editedField()); };
    const bool holding = std::ranges::any_of(instanceRows, [&](const InstanceRow& row) { return std::ranges::any_of(row.numbers, busy); }) ||
                         instanceColorRow.has_value();
    if (ended || !holding)
    {
        commitInstanceEdit(state, edited);
    }
}

void InspectorUi::commitInstanceEdit(ToolsState& state, const scene::Scene& edited)
{
    if (!instanceEdit)
    {
        return;
    }
    const InstanceEdit edit = *std::exchange(instanceEdit, std::nullopt);
    const Entity entity = edited.findEntity(edit.entity);
    const void* const component = entity.isValid() ? edit.type->find(edited, entity) : nullptr;
    if (component == nullptr)
    {
        return;
    }
    std::vector<std::unique_ptr<Command>> commands;
    const std::string componentName(edit.type->name());
    for (const auto& [fieldName, before] : {std::pair{scene::instanceShaderParametersField, &edit.names},
                                            std::pair{scene::instanceShaderValuesField, &edit.values}})
    {
        const reflection::FieldInfo* const field = edit.type->type->findField(fieldName);
        serialization::TextValue after = scene::writeFieldValue(*field, field->address(component));
        if (after != *before)
        {
            commands.push_back(makeSetFieldCommand(edit.entity, componentName, std::string(fieldName), *before, std::move(after)));
        }
    }
    if (!commands.empty())
    {
        state.history.recordApplied(makeCompositeCommand(std::move(commands), "Edit Instance Shader Parameters"));
    }
}

} // namespace devex::tools::detail
