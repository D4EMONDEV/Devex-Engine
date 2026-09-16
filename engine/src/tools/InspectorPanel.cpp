#include "ToolsState.hpp"

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

// Draws the widget for a field value and reports whether it changed the value this frame.
bool drawValueWidget(ToolsState& state, const char* label, ValueKind kind, void* address)
{
    switch (kind)
    {
    case ValueKind::Bool:
        return ImGui::Checkbox(label, static_cast<bool*>(address));
    case ValueKind::Int32:
        return ImGui::DragScalar(label, ImGuiDataType_S32, address, 0.1f);
    case ValueKind::UInt32:
        return ImGui::DragScalar(label, ImGuiDataType_U32, address, 0.1f);
    case ValueKind::Float:
        return ImGui::DragFloat(label, static_cast<float*>(address), 0.01f);
    case ValueKind::String:
        return ImGui::InputText(label, static_cast<std::string*>(address));
    case ValueKind::Vec2:
        return ImGui::DragFloat2(label, &(*static_cast<math::Vec2*>(address))[0], 0.01f);
    case ValueKind::Vec3:
        return ImGui::DragFloat3(label, &(*static_cast<math::Vec3*>(address))[0], 0.01f);
    case ValueKind::Vec4:
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
    case ValueKind::AssetId: {
        auto& id = *static_cast<asset::AssetId*>(address);
        std::string preview = id.isValid() ? id.uuid.toString() : "(none)";
        for (const BuiltinAsset& builtin : builtinMeshes)
        {
            preview = builtin.id == id ? builtin.name : preview;
        }
        bool changed = false;
        if (ImGui::BeginCombo(label, preview.c_str()))
        {
            for (const BuiltinAsset& builtin : builtinMeshes)
            {
                if (ImGui::Selectable(builtin.name, builtin.id == id) && builtin.id != id)
                {
                    id = builtin.id;
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
    serialization::TextValue before = scene::writeFieldValue(field.kind, address);

    const std::string label = displayName(field.name);
    const bool changed = drawValueWidget(state, label.c_str(), field.kind, address);

    if (ImGui::IsItemActivated())
    {
        state.fieldEditStart = before;
    }
    // Drags and text edits become one undo step when released; combos change in one click.
    const bool finishedEdit = ImGui::IsItemDeactivatedAfterEdit() ||
                              (changed && field.kind == ValueKind::AssetId);
    if (finishedEdit)
    {
        serialization::TextValue start =
            field.kind == ValueKind::AssetId ? std::move(before) : state.fieldEditStart;
        state.history.recordApplied(makeSetFieldCommand(entity, std::string(type.name()),
                                                        field.name, std::move(start),
                                                        scene::writeFieldValue(field.kind, address)));
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
