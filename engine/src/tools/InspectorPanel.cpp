#include "ToolsState.hpp"

#include <devex/scene/SceneSerializer.hpp>

#include <format>

#include <devex/asset/AssetId.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <array>
#include <cstdint>
#include <string_view>

namespace devex::tools::detail {
namespace {

using reflection::ValueKind;

struct BuiltinAsset
{
    const char* name;
    asset::AssetId id;
};

constexpr std::array builtinMeshes{
    BuiltinAsset{"Cube", asset::builtin::cubeMesh},
    BuiltinAsset{"Sphere", asset::builtin::sphereMesh},
    BuiltinAsset{"Plane", asset::builtin::planeMesh},
};

[[nodiscard]] std::string assetLabel(const ToolsState& state, asset::AssetId id)
{
    if (!id.isValid())
    {
        return "(none)";
    }
    for (const BuiltinAsset& builtin : builtinMeshes)
    {
        if (builtin.id == id)
        {
            return builtin.name;
        }
    }
    if (const asset::AssetInfo* const info =
            state.database != nullptr ? state.database->find(id) : nullptr)
    {
        return info->name;
    }
    return id.uuid.toString();
}

// A combo listing the assets of the expected type, which also accepts dropped assets.
bool drawAssetPicker(ToolsState& state, const char* label, const reflection::FieldInfo& field,
                     asset::AssetId& id)
{
    const std::optional<asset::AssetType> type =
        field.assetType.empty() ? std::nullopt : asset::parseAssetType(field.assetType);
    bool changed = false;
    const auto choose = [&](const char* name, asset::AssetId candidate) {
        ImGui::PushID(name);
        if (ImGui::Selectable(name, candidate == id) && candidate != id)
        {
            id = candidate;
            changed = true;
        }
        ImGui::PopID();
    };

    const std::string preview = assetLabel(state, id);
    if (ImGui::BeginCombo(label, preview.c_str(), ImGuiComboFlags_HeightLarge))
    {
        choose("(none)", asset::AssetId{});
        if (!type || *type == asset::AssetType::Mesh)
        {
            for (const BuiltinAsset& builtin : builtinMeshes)
            {
                choose(builtin.name, builtin.id);
            }
        }
        if (state.database != nullptr)
        {
            ImGui::Separator();
            for (const asset::AssetInfo& info : state.database->assets(type))
            {
                ImGui::PushID(info.id.uuid.toString().c_str());
                choose(info.name.c_str(), info.id);
                ImGui::PopID();
            }
        }
        ImGui::EndCombo();
    }
    if (const std::optional<asset::AssetId> dropped = acceptDroppedAsset(type);
        dropped && *dropped != id)
    {
        id = *dropped;
        changed = true;
    }
    return changed;
}

// Draws the widget for a field value and reports whether it changed the value this frame.
bool drawValueWidget(ToolsState& state, const char* label, const reflection::FieldInfo& field,
                     void* address)
{
    switch (field.kind)
    {
    case ValueKind::Bool:
        return ImGui::Checkbox(label, static_cast<bool*>(address));
    case ValueKind::Int32:
        return ImGui::DragScalar(label, ImGuiDataType_S32, address, 0.1f);
    case ValueKind::UInt32:
        return ImGui::DragScalar(label, ImGuiDataType_U32, address, 0.1f);
    case ValueKind::Float:
        if (field.angle)
        {
            float degrees = math::degrees(*static_cast<float*>(address));
            const bool changed = ImGui::DragFloat(label, &degrees, 0.5f, 0.0f, 0.0f, "%.1f deg");
            if (changed)
            {
                *static_cast<float*>(address) = math::radians(degrees);
            }
            return changed;
        }
        return ImGui::DragFloat(label, static_cast<float*>(address), 0.01f);
    case ValueKind::String:
        return ImGui::InputText(label, static_cast<std::string*>(address));
    case ValueKind::Vec2:
        return ImGui::DragFloat2(label, &(*static_cast<math::Vec2*>(address))[0], 0.01f);
    case ValueKind::Vec3:
        if (field.color)
        {
            return ImGui::ColorEdit3(label, &(*static_cast<math::Vec3*>(address))[0],
                                     ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        }
        return ImGui::DragFloat3(label, &(*static_cast<math::Vec3*>(address))[0], 0.01f);
    case ValueKind::Vec4:
        if (field.color)
        {
            return ImGui::ColorEdit4(label, &(*static_cast<math::Vec4*>(address))[0],
                                     ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        }
        return ImGui::DragFloat4(label, &(*static_cast<math::Vec4*>(address))[0], 0.01f);
    case ValueKind::Quat: {
        auto& rotation = *static_cast<math::Quat*>(address);
        const ImGuiID id = ImGui::GetID(label);
        math::Vec3 degrees = state.eulerEditId == id
                                 ? state.eulerEditDegrees
                                 : math::degrees(math::eulerAngles(rotation));
        const bool changed = ImGui::DragFloat3(label, &degrees[0], 0.5f);
        if (changed)
        {
            rotation = math::quatFromEulerAngles(math::radians(degrees));
        }
        if (ImGui::IsItemActive())
        {
            state.eulerEditId = id;
            state.eulerEditDegrees = degrees;
        }
        else if (state.eulerEditId == id)
        {
            state.eulerEditId = 0;
        }
        return changed;
    }
    case ValueKind::Uuid: {
        const std::string text = static_cast<core::Uuid*>(address)->toString();
        ImGui::LabelText(label, "%s", text.c_str());
        return false;
    }
    case ValueKind::AssetId:
        return drawAssetPicker(state, label, field, *static_cast<asset::AssetId*>(address));
    case ValueKind::Enum: {
        const std::uint32_t current = reflection::readEnumIndex(field, address);
        bool changed = false;
        const std::string preview = current < field.enumNames.size()
                                        ? displayName(field.enumNames[current])
                                        : std::to_string(current);
        if (ImGui::BeginCombo(label, preview.c_str()))
        {
            for (std::uint32_t index = 0; index < field.enumNames.size(); ++index)
            {
                if (ImGui::Selectable(displayName(field.enumNames[index]).c_str(), index == current) &&
                    index != current)
                {
                    reflection::writeEnumIndex(field, address, index);
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }
    }
    return false;
}

void drawField(ToolsState& state, core::Uuid entity, const scene::ComponentType& type,
               const reflection::FieldInfo& field, void* component)
{
    void* const address = field.address(component);
    // The value before this frame's change, which becomes the start of an edit on activation.
    serialization::TextValue before = scene::writeFieldValue(field, address);

    const std::string label = displayName(field.name);
    const bool changed = drawValueWidget(state, label.c_str(), field, address);

    if (ImGui::IsItemActivated())
    {
        state.fieldEditStart = before;
    }
    // Drags and text edits become one undo step when released; combos change in one click.
    const bool oneClickEdit = field.kind == ValueKind::AssetId || field.kind == ValueKind::Enum;
    const bool finishedEdit = ImGui::IsItemDeactivatedAfterEdit() || (changed && oneClickEdit);
    if (finishedEdit)
    {
        serialization::TextValue start =
            oneClickEdit ? std::move(before) : state.fieldEditStart;
        state.history.recordApplied(makeSetFieldCommand(entity, std::string(type.name()),
                                                        field.name, std::move(start),
                                                        scene::writeFieldValue(field, address)));
    }
}

void drawNameField(ToolsState& state, scene::Scene& scene, scene::Entity entity, core::Uuid uuid)
{
    // The buffer follows the scene except while the user is typing in it.
    const ImGuiID nameId = ImGui::GetID("Name");
    if (state.nameBufferEntity != uuid || ImGui::GetActiveID() != nameId)
    {
        state.nameBuffer = scene.name(entity);
        state.nameBufferEntity = uuid;
    }
    ImGui::InputText("Name", &state.nameBuffer);
    if (ImGui::IsItemActivated())
    {
        state.nameEditStart = scene.name(entity);
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && state.nameBuffer != state.nameEditStart)
    {
        state.pendingCommand = makeRenameCommand(uuid, state.nameEditStart, state.nameBuffer);
    }
}

} // namespace

void drawInspectorPanel(ToolsState& state, scene::Scene& scene)
{
    if (ImGui::Begin(inspectorWindow, &state.showInspector))
    {
        const scene::Entity entity = scene.findEntity(state.selection);
        if (!entity.isValid())
        {
            ImGui::TextDisabled("Select an entity in the hierarchy.");
            ImGui::End();
            return;
        }

        const core::Uuid uuid = state.selection;
        drawNameField(state, scene, entity, uuid);
        const std::string uuidText = uuid.toString();
        ImGui::TextDisabled("UUID %s", uuidText.c_str());
        ImGui::Separator();

        for (const scene::ComponentType& type : scene::componentRegistry().types())
        {
            const void* const component = type.find(scene, entity);
            if (component == nullptr)
            {
                continue;
            }

            const std::string name(type.name());
            ImGui::PushID(name.c_str());
            bool keep = true;
            if (ImGui::CollapsingHeader(name.c_str(), &keep, ImGuiTreeNodeFlags_DefaultOpen))
            {
                for (const reflection::FieldInfo& field : type.type->fields)
                {
                    drawField(state, uuid, type, field, const_cast<void*>(component));
                }
            }
            if (!keep)
            {
                state.pendingCommand = makeRemoveComponentCommand(uuid, name);
            }
            ImGui::PopID();
        }

        // Components whose type is not registered, such as those of game code that is not loaded.
        if (const scene::PreservedComponents* const preserved = scene.tryGet<scene::PreservedComponents>(entity))
        {
            for (const serialization::TextSection& section : preserved->sections)
            {
                const serialization::TextValue* const typeValue = section.findAttribute("type");
                const std::string* const typeName = typeValue != nullptr ? serialization::asString(*typeValue) : nullptr;
                ImGui::BeginDisabled();
                ImGui::CollapsingHeader(std::format("{} (unavailable)##preserved{}", typeName != nullptr ? *typeName : "?",
                                                    static_cast<const void*>(&section))
                                            .c_str(),
                                        ImGuiTreeNodeFlags_Leaf);
                ImGui::EndDisabled();
                ImGui::SetItemTooltip("The game code that defines this component is not loaded. It is kept in the "
                                      "scene and comes back with the code.");
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("Add component", ImVec2(-1.0f, 0.0f)))
        {
            ImGui::OpenPopup("add component");
        }
        if (ImGui::BeginPopup("add component"))
        {
            for (const scene::ComponentType& type : scene::componentRegistry().types())
            {
                if (type.find(scene, entity) == nullptr &&
                    ImGui::Selectable(std::string(type.name()).c_str()))
                {
                    state.pendingCommand = makeAddComponentCommand(uuid, std::string(type.name()));
                }
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}

} // namespace devex::tools::detail
