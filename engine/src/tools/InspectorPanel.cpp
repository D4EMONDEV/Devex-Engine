#include "ToolsState.hpp"

#include <devex/asset/AssetId.hpp>
#include <devex/asset/Project.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <array>
#include <cctype>
#include <cfloat>
#include <cstdint>
#include <format>
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
    if (const asset::AssetInfo* const info = state.database != nullptr ? state.database->find(id) : nullptr)
    {
        return info->name;
    }
    return id.uuid.toString();
}

// A combo listing the assets of the expected type, which also accepts dropped assets.
bool drawAssetPicker(ToolsState& state, const char* id, const reflection::FieldInfo& field, asset::AssetId& value)
{
    const std::optional<asset::AssetType> type =
        field.assetType.empty() ? std::nullopt : asset::parseAssetType(field.assetType);
    bool changed = false;
    const auto choose = [&](const char* name, asset::AssetId candidate) {
        ImGui::PushID(name);
        if (ImGui::Selectable(name, candidate == value) && candidate != value)
        {
            value = candidate;
            changed = true;
        }
        ImGui::PopID();
    };

    const std::string preview = assetLabel(state, value);
    if (beginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge))
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
    if (const std::optional<asset::AssetId> dropped = acceptDroppedAsset(type); dropped && *dropped != value)
    {
        value = *dropped;
        changed = true;
    }
    return changed;
}

// A combo of the named collision layers of the project.
bool drawLayerPicker(const ToolsState& state, const char* id, std::uint32_t& layer)
{
    const asset::PhysicsSettings settings = state.database != nullptr ? state.database->project().physics : asset::PhysicsSettings{};
    const auto label = [&](std::uint32_t index) {
        const std::string& name = index < settings.layerNames.size() ? settings.layerNames[index] : std::string();
        return name.empty() ? std::format("{}: (unused)", index) : std::format("{}: {}", index, name);
    };
    bool changed = false;
    if (beginCombo(id, label(layer).c_str()))
    {
        for (std::uint32_t index = 0; index < settings.layerNames.size(); ++index)
        {
            if (index != layer && index != 0 && settings.layerNames[index].empty())
            {
                continue;
            }
            if (ImGui::Selectable(label(index).c_str(), index == layer) && index != layer)
            {
                layer = index;
                changed = true;
            }
        }
        ImGui::Separator();
        ImGui::TextDisabled("Name layers in Project > Project Settings");
        ImGui::EndCombo();
    }
    return changed;
}

// Draws the widget for a field value and reports whether it changed the value this frame.
bool drawValueWidget(ToolsState& state, const char* id, const reflection::FieldInfo& field, void* address)
{
    switch (field.kind)
    {
    case ValueKind::Bool:
        return ImGui::Checkbox(id, static_cast<bool*>(address));
    case ValueKind::Int32:
        return ImGui::DragScalar(id, ImGuiDataType_S32, address, 0.1f);
    case ValueKind::UInt32:
        if (field.physicsLayer)
        {
            return drawLayerPicker(state, id, *static_cast<std::uint32_t*>(address));
        }
        return ImGui::DragScalar(id, ImGuiDataType_U32, address, 0.1f);
    case ValueKind::Float:
        if (field.angle)
        {
            float degrees = math::degrees(*static_cast<float*>(address));
            const bool changed = ImGui::DragFloat(id, &degrees, 0.5f, 0.0f, 0.0f, "%.1f°");
            if (changed)
            {
                *static_cast<float*>(address) = math::radians(degrees);
            }
            return changed;
        }
        return ImGui::DragFloat(id, static_cast<float*>(address), 0.01f, 0.0f, 0.0f, "%.3f");
    case ValueKind::String:
        return ImGui::InputText(id, static_cast<std::string*>(address));
    case ValueKind::Vec2:
        return dragVector(id, &(*static_cast<math::Vec2*>(address))[0], 2, 0.01f);
    case ValueKind::Vec3:
        if (field.color)
        {
            return ImGui::ColorEdit3(id, &(*static_cast<math::Vec3*>(address))[0],
                                     ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        }
        return dragVector(id, &(*static_cast<math::Vec3*>(address))[0], 3, 0.01f);
    case ValueKind::Vec4:
        if (field.color)
        {
            return ImGui::ColorEdit4(id, &(*static_cast<math::Vec4*>(address))[0],
                                     ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        }
        return dragVector(id, &(*static_cast<math::Vec4*>(address))[0], 4, 0.01f);
    case ValueKind::Quat: {
        auto& rotation = *static_cast<math::Quat*>(address);
        const ImGuiID widget = ImGui::GetID(id);
        math::Vec3 degrees = state.eulerEditId == widget ? state.eulerEditDegrees : math::degrees(math::eulerAngles(rotation));
        const bool changed = dragVector(id, &degrees[0], 3, 0.5f, "%.1f°");
        if (changed)
        {
            rotation = math::quatFromEulerAngles(math::radians(degrees));
        }
        if (ImGui::IsItemActive())
        {
            state.eulerEditId = widget;
            state.eulerEditDegrees = degrees;
        }
        else if (state.eulerEditId == widget)
        {
            state.eulerEditId = 0;
        }
        return changed;
    }
    case ValueKind::Uuid: {
        const std::string text = static_cast<core::Uuid*>(address)->toString();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", text.c_str());
        return false;
    }
    case ValueKind::AssetId:
        return drawAssetPicker(state, id, field, *static_cast<asset::AssetId*>(address));
    case ValueKind::Enum: {
        const std::uint32_t current = reflection::readEnumIndex(field, address);
        bool changed = false;
        const std::string preview =
            current < field.enumNames.size() ? displayName(field.enumNames[current]) : std::to_string(current);
        if (beginCombo(id, preview.c_str()))
        {
            for (std::uint32_t index = 0; index < field.enumNames.size(); ++index)
            {
                if (ImGui::Selectable(displayName(field.enumNames[index]).c_str(), index == current) && index != current)
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
    propertyName(label.c_str());
    const std::string id = "##" + std::string(field.name);
    const bool changed = drawValueWidget(state, id.c_str(), field, address);

    if (ImGui::IsItemActivated())
    {
        state.fieldEditStart = before;
    }
    // Drags and text edits become one undo step when released; combos change in one click.
    const bool oneClickEdit = field.kind == ValueKind::AssetId || field.kind == ValueKind::Enum || field.physicsLayer;
    const bool finishedEdit = ImGui::IsItemDeactivatedAfterEdit() || (changed && oneClickEdit);
    if (finishedEdit)
    {
        serialization::TextValue start = oneClickEdit ? std::move(before) : state.fieldEditStart;
        state.history.recordApplied(makeSetFieldCommand(entity, std::string(type.name()), field.name, std::move(start),
                                                        scene::writeFieldValue(field, address)));
    }
}

void drawNameField(ToolsState& state, scene::Scene& scene, scene::Entity entity, core::Uuid uuid)
{
    // The buffer follows the scene except while the user is typing in it.
    const ImGuiID nameId = ImGui::GetID("##name");
    if (state.nameBufferEntity != uuid || ImGui::GetActiveID() != nameId)
    {
        state.nameBuffer = scene.name(entity);
        state.nameBufferEntity = uuid;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##name", "Name", &state.nameBuffer);
    if (ImGui::IsItemActivated())
    {
        state.nameEditStart = scene.name(entity);
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && state.nameBuffer != state.nameEditStart)
    {
        state.pendingCommand = makeRenameCommand(uuid, state.nameEditStart, state.nameBuffer);
    }
}

// A section header: the component's icon and name, folding its properties, with a menu on the right.
// Returns whether the section is open; removed tells that its menu asked to remove the component.
[[nodiscard]] bool componentHeader(const char* name, EntityIcon icon, bool enabled, bool* removed)
{
    const ThemeColors& colors = themeColors();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleColor(ImGuiCol_Header, uiColor(colors.outer));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, uiColor(ImVec4(colors.outer.x, colors.outer.y, colors.outer.z, 1.0f)));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, uiColor(colors.outer));
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const bool open = ImGui::TreeNodeEx("##header", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen |
                                                        ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap |
                                                        ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_NoTreePushOnOpen);
    ImGui::PopStyleColor(3);
    const float height = ImGui::GetItemRectSize().y;
    const float width = ImGui::GetItemRectSize().x;
    const float textY = start.y + (height - ImGui::GetFontSize()) * 0.5f;
    const float iconX = start.x + ImGui::GetTreeNodeToLabelSpacing();
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    draw->AddText(ImVec2(iconX, textY), uiColorU32(enabled ? icon.color : colors.textDim), icon.icon.c_str());
    draw->AddText(editorFonts().bold, ImGui::GetFontSize(),
                  ImVec2(iconX + ImGui::CalcTextSize(icon.icon.c_str()).x + style.ItemInnerSpacing.x * 1.5f, textY),
                  ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled), name);

    if (removed != nullptr)
    {
        const float buttonWidth = toolButtonWidth();
        ImGui::SetCursorScreenPos(ImVec2(start.x + width - buttonWidth, start.y + (height - buttonWidth) * 0.5f));
        if (toolButton("menu", icons::Ellipsis, nullptr))
        {
            ImGui::OpenPopup("component menu");
        }
        if (ImGui::BeginPopup("component menu"))
        {
            if (ImGui::MenuItemEx("Remove Component", icons::Trash.c_str()))
            {
                *removed = true;
            }
            ImGui::EndPopup();
        }
        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + height + style.ItemSpacing.y));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }
    return open;
}

[[nodiscard]] bool containsIgnoringCase(std::string_view text, std::string_view part)
{
    return part.empty() || !std::ranges::search(text, part, [](char left, char right) {
                                return std::tolower(static_cast<unsigned char>(left)) ==
                                       std::tolower(static_cast<unsigned char>(right));
                            }).empty();
}

void drawAddComponent(ToolsState& state, scene::Scene& scene, scene::Entity entity, core::Uuid uuid)
{
    static std::string filter;
    ImGui::Spacing();
    if (labelButton(icons::Plus, "Add Component", -FLT_MIN))
    {
        filter.clear();
        ImGui::OpenPopup("add component");
    }
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetItemRectSize().x, 0.0f));
    if (ImGui::BeginPopup("add component"))
    {
        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }
        searchField("##filter", filter, "Search Components");
        std::size_t shown = 0;
        for (const scene::ComponentType& type : scene::componentRegistry().types())
        {
            const std::string name(type.name());
            if (type.find(scene, entity) != nullptr || !containsIgnoringCase(name, filter))
            {
                continue;
            }
            ++shown;
            const EntityIcon icon = componentIcon(name);
            const ImVec2 position = ImGui::GetCursorScreenPos();
            const std::string label = std::format("      {}", name);
            if (ImGui::Selectable(label.c_str()))
            {
                state.pendingCommand = makeAddComponentCommand(uuid, name);
            }
            ImGui::GetWindowDrawList()->AddText(position, uiColorU32(icon.color), icon.icon.c_str());
        }
        if (shown == 0)
        {
            ImGui::TextDisabled("No component to add.");
        }
        ImGui::EndPopup();
    }
}

} // namespace

void drawInspectorPanel(ToolsState& state, scene::Scene& scene)
{
    if (ImGui::Begin(inspectorWindow))
    {
        const scene::Entity entity = scene.findEntity(state.selection);
        if (!entity.isValid())
        {
            const char* const hint = "Select an entity to inspect it.";
            const ImVec2 size = ImGui::CalcTextSize(hint);
            ImGui::SetCursorPos(ImVec2(std::max(0.0f, (ImGui::GetWindowWidth() - size.x) * 0.5f), ImGui::GetWindowHeight() * 0.35f));
            ImGui::TextDisabled("%s", hint);
            ImGui::End();
            return;
        }

        const core::Uuid uuid = state.selection;
        const EntityIcon icon = entityIcon(scene, entity);
        ImGui::AlignTextToFramePadding();
        iconLabel(icon.icon, icon.color);
        drawNameField(state, scene, entity, uuid);
        const std::string uuidText = uuid.toString();
        ImGui::TextDisabled("%s", uuidText.c_str());
        ImGui::SetItemTooltip("The UUID of the entity, which scenes and undo steps refer to");
        ImGui::Spacing();

        for (const scene::ComponentType& type : scene::componentRegistry().types())
        {
            const void* const component = type.find(scene, entity);
            if (component == nullptr)
            {
                continue;
            }
            const std::string name(type.name());
            ImGui::PushID(name.c_str());
            bool removed = false;
            if (componentHeader(name.c_str(), componentIcon(name), true, &removed) && beginProperties("fields"))
            {
                for (const reflection::FieldInfo& field : type.type->fields)
                {
                    drawField(state, uuid, type, field, const_cast<void*>(component));
                }
                endProperties();
            }
            if (removed)
            {
                state.pendingCommand = makeRemoveComponentCommand(uuid, name);
            }
            ImGui::Spacing();
            ImGui::PopID();
        }

        // Components whose type is not registered, such as those of game code that is not loaded.
        if (const scene::PreservedComponents* const preserved = scene.tryGet<scene::PreservedComponents>(entity))
        {
            for (const serialization::TextSection& section : preserved->sections)
            {
                const serialization::TextValue* const typeValue = section.findAttribute("type");
                const std::string* const typeName = typeValue != nullptr ? serialization::asString(*typeValue) : nullptr;
                const std::string label = std::format("{} (not loaded)", typeName != nullptr ? *typeName : "?");
                ImGui::PushID(&section);
                static_cast<void>(componentHeader(label.c_str(), {icons::Puzzle, themeColors().gameCode}, false, nullptr));
                ImGui::SetItemTooltip("The game code that defines this component is not loaded. It is kept in the "
                                      "scene and comes back with the code.");
                ImGui::PopID();
            }
        }

        drawAddComponent(state, scene, entity, uuid);
    }
    ImGui::End();
}

} // namespace devex::tools::detail
