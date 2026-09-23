#include "ToolsState.hpp"

#include <devex/asset/AssetId.hpp>
#include <devex/asset/ThemeData.hpp>
#include <devex/asset/Project.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/tools/SceneCommands.hpp>
#include <devex/ui/Theme.hpp>

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <memory>
#include <optional>
#include <cfloat>
#include <cstdint>
#include <cstring>
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

} // namespace

bool drawAssetPicker(ToolsState& state, const char* id, std::optional<asset::AssetType> type, asset::AssetId& value,
                     bool mixed)
{
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

    const std::string preview = mixed ? std::string(mixedValue) : assetLabel(state, value);
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

namespace {

// A combo of the named collision layers of the project.
bool drawLayerPicker(const ToolsState& state, const char* id, std::uint32_t& layer, bool mixed)
{
    const asset::PhysicsSettings settings = state.database != nullptr ? state.database->project().physics : asset::PhysicsSettings{};
    const auto label = [&](std::uint32_t index) {
        const std::string& name = index < settings.layerNames.size() ? settings.layerNames[index] : std::string();
        return name.empty() ? std::format("{}: (unused)", index) : std::format("{}: {}", index, name);
    };
    bool changed = false;
    if (beginCombo(id, mixed ? mixedValue : label(layer).c_str()))
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

bool drawAudioGroupPicker(const ToolsState& state, const char* id, std::uint32_t& group, bool mixed)
{
    const asset::AudioSettings settings = state.database != nullptr ? state.database->project().audio : asset::AudioSettings{};
    const auto label = [&](std::uint32_t index) {
        const std::string& name = index < settings.groupNames.size() ? settings.groupNames[index] : std::string();
        return name.empty() ? std::format("{}: (unused)", index) : std::format("{}: {}", index, name);
    };
    bool changed = false;
    if (beginCombo(id, mixed ? mixedValue : label(group).c_str()))
    {
        for (std::uint32_t index = 0; index < settings.groupNames.size(); ++index)
        {
            if (index != group && settings.groupNames[index].empty())
            {
                continue;
            }
            if (ImGui::Selectable(label(index).c_str(), index == group) && index != group)
            {
                group = index;
                changed = true;
            }
        }
        ImGui::Separator();
        ImGui::TextDisabled("Name groups in Project > Project Settings");
        ImGui::EndCombo();
    }
    return changed;
}

// Adds the entities of a subtree to the menu of an entity picker, indented by depth.
bool chooseEntities(const scene::Scene& scene, scene::Entity entity, int depth, scene::EntityRef& value)
{
    bool changed = false;
    for (; entity.isValid(); entity = scene.nextSibling(entity))
    {
        const scene::EntityRef candidate = scene.reference(entity);
        const std::string label = std::string(static_cast<std::size_t>(depth) * 2, ' ') + scene.name(entity);
        ImGui::PushID(candidate.uuid.toString().c_str());
        if (ImGui::Selectable(label.c_str(), candidate == value) && candidate != value)
        {
            value = candidate;
            changed = true;
        }
        ImGui::PopID();
        changed |= chooseEntities(scene, scene.firstChild(entity), depth + 1, value);
    }
    return changed;
}

// An entity of the scene, chosen from a menu or dropped from the scene tree.
bool drawEntityPicker(const scene::Scene& scene, const char* id, scene::EntityRef& value, bool mixed)
{
    const scene::Entity target = scene.resolve(value);
    const std::string preview = mixed          ? std::string(mixedValue)
                                : value.isNil() ? "(none)"
                                : target.isValid() ? scene.name(target)
                                                   : "(missing)";
    bool changed = false;
    if (beginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge))
    {
        if (ImGui::Selectable("(none)", value.isNil()) && !value.isNil())
        {
            value = {};
            changed = true;
        }
        ImGui::Separator();
        changed |= chooseEntities(scene, scene.firstRoot(), 0, value);
        ImGui::EndCombo();
    }
    if (!value.isNil())
    {
        ImGui::SetItemTooltip("%s", value.uuid.toString().c_str());
    }
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* const payload = ImGui::AcceptDragDropPayload(entityPayload);
            payload != nullptr && payload->DataSize == 16)
        {
            std::array<std::uint8_t, 16> bytes{};
            std::memcpy(bytes.data(), payload->Data, bytes.size());
            const scene::EntityRef dropped{uuidFromBytes(bytes)};
            if (dropped != value)
            {
                value = dropped;
                changed = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    return changed;
}

// Draws the widget for a field value and reports whether it changed the value this frame. With
// several entities selected, a mixed value shows a dash, and so do the components of a vector whose
// bit is set in mixedComponents.
bool drawValueWidget(ToolsState& state, const scene::Scene& scene, const char* id, const reflection::FieldInfo& field,
                     void* address, bool mixed = false, unsigned mixedComponents = 0)
{
    switch (field.kind)
    {
    case ValueKind::Bool: {
        ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixed);
        const bool changed = ImGui::Checkbox(id, static_cast<bool*>(address));
        ImGui::PopItemFlag();
        return changed;
    }
    case ValueKind::Int32:
        return ImGui::DragScalar(id, ImGuiDataType_S32, address, 0.1f, nullptr, nullptr, mixed ? mixedValue : "%d");
    case ValueKind::UInt32:
        if (field.physicsLayer)
        {
            return drawLayerPicker(state, id, *static_cast<std::uint32_t*>(address), mixed);
        }
        if (field.audioGroup)
        {
            return drawAudioGroupPicker(state, id, *static_cast<std::uint32_t*>(address), mixed);
        }
        return ImGui::DragScalar(id, ImGuiDataType_U32, address, 0.1f, nullptr, nullptr, mixed ? mixedValue : "%u");
    case ValueKind::Float:
        if (field.angle)
        {
            float degrees = math::degrees(*static_cast<float*>(address));
            const bool changed = ImGui::DragFloat(id, &degrees, 0.5f, 0.0f, 0.0f, mixed ? mixedValue : "%.1f°");
            if (changed)
            {
                *static_cast<float*>(address) = math::radians(degrees);
            }
            return changed;
        }
        return ImGui::DragFloat(id, static_cast<float*>(address), 0.01f, 0.0f, 0.0f, mixed ? mixedValue : "%.3f");
    case ValueKind::String:
        if (mixed)
        {
            // Empty with a dash until something is typed, which then goes to every entity.
            if (ImGui::GetActiveID() != ImGui::GetID(id))
            {
                state.mixedTextBuffer.clear();
            }
            if (ImGui::InputTextWithHint(id, mixedValue, &state.mixedTextBuffer))
            {
                *static_cast<std::string*>(address) = state.mixedTextBuffer;
                return true;
            }
            return false;
        }
        return ImGui::InputText(id, static_cast<std::string*>(address));
    case ValueKind::Vec2:
        return dragVector(id, &(*static_cast<math::Vec2*>(address))[0], 2, 0.01f, "%.3f", mixedComponents);
    case ValueKind::Vec3:
        if (field.color)
        {
            return ImGui::ColorEdit3(id, &(*static_cast<math::Vec3*>(address))[0],
                                     ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        }
        return dragVector(id, &(*static_cast<math::Vec3*>(address))[0], 3, 0.01f, "%.3f", mixedComponents);
    case ValueKind::Vec4:
        if (field.color)
        {
            return ImGui::ColorEdit4(id, &(*static_cast<math::Vec4*>(address))[0],
                                     ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        }
        return dragVector(id, &(*static_cast<math::Vec4*>(address))[0], 4, 0.01f, "%.3f", mixedComponents);
    case ValueKind::Quat: {
        auto& rotation = *static_cast<math::Quat*>(address);
        const ImGuiID widget = ImGui::GetID(id);
        math::Vec3 degrees = state.eulerEditId == widget ? state.eulerEditDegrees : math::degrees(math::eulerAngles(rotation));
        const bool changed = dragVector(id, &degrees[0], 3, 0.5f, "%.1f°", mixedComponents);
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
        return drawAssetPicker(state, id, field.assetType.empty() ? std::nullopt : asset::parseAssetType(field.assetType),
                               *static_cast<asset::AssetId*>(address), mixed);
    case ValueKind::Enum: {
        const std::uint32_t current = reflection::readEnumIndex(field, address);
        bool changed = false;
        const std::string preview = mixed ? std::string(mixedValue)
                                    : current < field.enumNames.size() ? displayName(field.enumNames[current])
                                                                        : std::to_string(current);
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
    case ValueKind::Entity:
        return drawEntityPicker(scene, id, *static_cast<scene::EntityRef*>(address), mixed);
    }
    return false;
}

// Whether a change to a value of the field is complete in one click, rather than at the end of a
// drag or of typing.
[[nodiscard]] bool isOneClickEdit(const reflection::FieldInfo& field) noexcept
{
    return field.kind == ValueKind::AssetId || field.kind == ValueKind::Enum || field.kind == ValueKind::Entity ||
           field.physicsLayer || field.audioGroup;
}

// A list: its size and a button to add an element, then a row per element with a button to
// remove it. Every change is one undo step of the whole list.
void drawListField(ToolsState& state, const scene::Scene& scene, core::Uuid entity, const scene::ComponentType& type,
                   const reflection::FieldInfo& field, void* address, serialization::TextValue before, bool highlighted)
{
    const std::string label = displayName(field.name);
    const std::size_t count = field.list->size(address);
    propertyName(label.c_str(), highlighted);
    ImGui::PushID(field.name.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%zu %s", count, count == 1 ? "element" : "elements");
    ImGui::SameLine();
    alignRight(toolButtonWidth());
    bool structural = false;
    if (toolButton("add", icons::Plus, "Add an element"))
    {
        field.list->resize(address, count + 1);
        structural = true;
    }

    bool activated = false;
    bool finished = false;
    bool oneClick = false;
    for (std::size_t index = 0; index < count && !structural; ++index)
    {
        ImGui::PushID(static_cast<int>(index));
        const std::string elementName = "    " + std::to_string(index);
        propertyName(elementName.c_str());
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - toolButtonWidth() - ImGui::GetStyle().ItemSpacing.x);
        const bool changed = drawValueWidget(state, scene, "##element", field, field.list->element(address, index));
        activated |= ImGui::IsItemActivated();
        finished |= ImGui::IsItemDeactivatedAfterEdit();
        oneClick |= changed && isOneClickEdit(field);
        ImGui::SameLine();
        if (toolButton("remove", icons::Minus, "Remove this element"))
        {
            field.list->erase(address, index);
            structural = true;
        }
        ImGui::PopID();
    }
    ImGui::PopID();

    if (activated)
    {
        state.fieldEditStart = before;
    }
    if (structural || oneClick || finished)
    {
        serialization::TextValue start = structural || oneClick ? std::move(before) : state.fieldEditStart;
        state.history.recordApplied(makeSetFieldCommand(entity, std::string(type.name()), field.name, std::move(start),
                                                        scene::writeFieldValue(field, address)));
    }
}

// Draws a field, marked when its value differs from the one of the prefab's component, if any.
void drawField(ToolsState& state, const scene::Scene& scene, core::Uuid entity, const scene::ComponentType& type,
               const reflection::FieldInfo& field, void* component, const void* prefabComponent,
               const ui::ElementStyle& style)
{
    void* const address = field.address(component);
    // The value before this frame's change, which becomes the start of an edit on activation.
    serialization::TextValue before = scene::writeFieldValue(field, address);
    std::optional<serialization::TextValue> prefabValue;
    if (prefabComponent != nullptr)
    {
        if (serialization::TextValue value = scene::writeFieldValue(field, field.address(prefabComponent)); value != before)
        {
            prefabValue = std::move(value);
        }
    }

    if (field.list != nullptr)
    {
        drawListField(state, scene, entity, type, field, address, std::move(before), prefabValue.has_value());
        return;
    }
    const std::string label = displayName(field.name);
    propertyName(label.c_str(), prefabValue.has_value());
    const std::string id = "##" + std::string(field.name);
    // A field the style of the element sets is written by the theme every frame: it shows the
    // value of the theme and cannot be changed here, which would not hold.
    const bool themed = style.sets(type.name(), field.name);
    if (themed)
    {
        ImGui::BeginDisabled();
    }
    const bool changed = drawValueWidget(state, scene, id.c_str(), field, address);
    if (themed)
    {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
        {
            ImGui::SetTooltip("Set by the style '%s' of the theme of the canvas.\n"
                              "Change it in the theme, or leave the style to set it here.",
                              style.name.c_str());
        }
    }
    if (prefabValue && state.playState == PlayState::Editing)
    {
        ImGui::PushID(id.c_str());
        if (ImGui::BeginPopupContextItem("override menu"))
        {
            if (ImGui::MenuItemEx("Revert to Prefab Value", icons::Undo.c_str()))
            {
                state.pendingCommand =
                    makeSetFieldCommand(entity, std::string(type.name()), field.name, before, *prefabValue);
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    if (ImGui::IsItemActivated())
    {
        state.fieldEditStart = before;
    }
    // Drags and text edits become one undo step when released; combos change in one click.
    const bool oneClickEdit = isOneClickEdit(field);
    const bool finishedEdit = ImGui::IsItemDeactivatedAfterEdit() || (changed && oneClickEdit);
    if (finishedEdit)
    {
        serialization::TextValue start = oneClickEdit ? std::move(before) : state.fieldEditStart;
        state.history.recordApplied(makeSetFieldCommand(entity, std::string(type.name()), field.name, std::move(start),
                                                        scene::writeFieldValue(field, address)));
    }
}

// The name field; prefabName is the name the entity has in its prefab, when it may differ.
void drawNameField(ToolsState& state, scene::Scene& scene, scene::Entity entity, core::Uuid uuid,
                   const std::string* prefabName)
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
    if (prefabName != nullptr && *prefabName != scene.name(entity))
    {
        const ImVec2 min = ImGui::GetItemRectMin();
        const float barWidth = std::max(2.0f, std::round(ImGui::GetFontSize() * 0.16f));
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(min.x - barWidth * 2.0f, min.y),
                                                  ImVec2(min.x - barWidth, ImGui::GetItemRectMax().y),
                                                  uiColorU32(themeColors().accent));
        if (state.playState == PlayState::Editing && ImGui::BeginPopupContextItem("name override"))
        {
            if (ImGui::MenuItemEx("Revert to Prefab Name", icons::Undo.c_str()))
            {
                state.pendingCommand = makeRenameCommand(uuid, scene.name(entity), *prefabName);
            }
            ImGui::EndPopup();
        }
    }
}

// What the entity has from a prefab: the instance, its prefab, and buttons for the instance.
void drawPrefabSection(ToolsState& state, scene::Scene& scene, scene::Entity entity, scene::Entity instance)
{
    const ThemeColors& colors = themeColors();
    const scene::PrefabInstance& prefab = scene.get<scene::PrefabInstance>(instance);
    const std::string prefabName = sceneAssetName(state, prefab.prefab);
    ImGui::AlignTextToFramePadding();
    iconLabel(icons::Package, prefab.resolved ? colors.prefab : colors.error);
    if (!prefab.resolved)
    {
        ImGui::TextColored(uiColor(colors.error), "Missing prefab %s", prefabName.c_str());
        ImGui::SetItemTooltip("The prefab cannot be loaded. The instance keeps its overrides until it comes back.");
    }
    else
    {
        ImGui::TextDisabled(entity == instance ? "Instance of" : "From");
        ImGui::SameLine();
        boldText(prefabName.c_str());
        if (entity != instance)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("in %s", scene.name(instance).c_str());
        }
    }

    const bool editing = state.playState == PlayState::Editing && state.mode == ToolsMode::Editor;
    if (labelButton(icons::ExternalLink, "Open", 0.0f, editing))
    {
        state.prefabToOpen = prefab.prefab;
    }
    ImGui::SetItemTooltip("Opens the prefab in a tab; its changes reach every instance once saved");
    ImGui::SameLine();
    if (labelButton(icons::Undo, "Revert All", 0.0f, editing && prefab.resolved))
    {
        state.pendingCommand = makeReplaceEntityTreeCommand(
            scene.uuid(instance), scene::saveRevertedPrefabInstance(scene, instance), "Revert instance");
    }
    ImGui::SetItemTooltip("Reverts every override of the instance except the placement of its root; added "
                          "entities stay");
    ImGui::SameLine();
    if (labelButton(icons::Unlink, "Make Local", 0.0f, editing && prefab.resolved))
    {
        state.pendingCommand = makeReplaceEntityTreeCommand(
            scene.uuid(instance), scene::saveUnpackedEntityTree(scene, instance), "Make instance local");
    }
    ImGui::SetItemTooltip("Turns the instance into ordinary entities, no longer linked to the prefab");
    ImGui::Spacing();
}

// A section header: the component's icon and name, folding its properties, with a menu on the right.
// Returns whether the section is open; removed tells that its menu asked to remove the component,
// which a component from a prefab cannot be. An added component, which its prefab does not have, is
// marked.
[[nodiscard]] bool componentHeader(const char* name, EntityIcon icon, bool enabled, bool* removed,
                                   bool fromPrefab = false, bool added = false)
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
    if (added)
    {
        const float barWidth = std::max(2.0f, std::round(ImGui::GetFontSize() * 0.16f));
        draw->AddRectFilled(start, ImVec2(start.x + barWidth, start.y + height), uiColorU32(colors.accent));
    }

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
            if (ImGui::MenuItemEx("Remove Component", icons::Trash.c_str(), nullptr, false, !fromPrefab))
            {
                *removed = true;
            }
            if (fromPrefab)
            {
                ImGui::SetItemTooltip("The component comes from the prefab");
            }
            else if (added)
            {
                ImGui::SetItemTooltip("The prefab does not have this component: removing it reverts the override");
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

// Whether the entity has the component from its prefab, which cannot remove it.
[[nodiscard]] bool hasFromPrefab(const scene::Scene& scene, scene::Entity entity, const scene::ComponentType& type)
{
    const scene::Entity instance = scene::owningPrefabInstance(scene, entity);
    if (!instance.isValid() || !scene.has<scene::PrefabEntity>(entity) || !scene.get<scene::PrefabInstance>(instance).resolved)
    {
        return false;
    }
    const std::shared_ptr<const scene::Scene> base =
        scene::prefabBase(scene.get<scene::PrefabInstance>(instance).prefab, scene.uuid(instance));
    const scene::Entity original = base != nullptr ? base->findEntity(scene.uuid(entity)) : scene::Entity{};
    return original.isValid() && type.find(*base, original) != nullptr;
}

// Which components of a vector differ between the value of the active entity and the others.
[[nodiscard]] unsigned mixedComponents(const reflection::FieldInfo& field, const void* active,
                                       const std::vector<const void*>& others)
{
    const auto components = [&field](const void* address) -> std::array<float, 4> {
        switch (field.kind)
        {
        case ValueKind::Vec2: {
            const auto& value = *static_cast<const math::Vec2*>(address);
            return {value.x, value.y, 0.0f, 0.0f};
        }
        case ValueKind::Vec3: {
            const auto& value = *static_cast<const math::Vec3*>(address);
            return {value.x, value.y, value.z, 0.0f};
        }
        case ValueKind::Vec4: {
            const auto& value = *static_cast<const math::Vec4*>(address);
            return {value.x, value.y, value.z, value.w};
        }
        case ValueKind::Quat: {
            const math::Vec3 degrees = math::degrees(math::eulerAngles(*static_cast<const math::Quat*>(address)));
            return {degrees.x, degrees.y, degrees.z, 0.0f};
        }
        default:
            return {};
        }
    };
    const std::array<float, 4> reference = components(active);
    unsigned mixed = 0;
    for (const void* other : others)
    {
        const std::array<float, 4> values = components(other);
        for (unsigned index = 0; index < 4; ++index)
        {
            if (std::abs(values[index] - reference[index]) > 1e-4f)
            {
                mixed |= 1u << index;
            }
        }
    }
    return mixed;
}

// Gives the other entity the components of the vector that the edit of the active one changed,
// keeping its own for the others.
void copyChangedComponents(const reflection::FieldInfo& field, const void* before, const void* after, void* other)
{
    const auto merge = [](auto& target, const auto& from, const auto& to, int count) {
        for (int index = 0; index < count; ++index)
        {
            if (from[index] != to[index])
            {
                target[index] = to[index];
            }
        }
    };
    switch (field.kind)
    {
    case ValueKind::Vec2:
        merge(*static_cast<math::Vec2*>(other), *static_cast<const math::Vec2*>(before),
              *static_cast<const math::Vec2*>(after), 2);
        break;
    case ValueKind::Vec3:
        merge(*static_cast<math::Vec3*>(other), *static_cast<const math::Vec3*>(before),
              *static_cast<const math::Vec3*>(after), 3);
        break;
    case ValueKind::Vec4:
        merge(*static_cast<math::Vec4*>(other), *static_cast<const math::Vec4*>(before),
              *static_cast<const math::Vec4*>(after), 4);
        break;
    case ValueKind::Quat: {
        // Angles, as the inspector shows them.
        const math::Vec3 from = math::degrees(math::eulerAngles(*static_cast<const math::Quat*>(before)));
        const math::Vec3 to = math::degrees(math::eulerAngles(*static_cast<const math::Quat*>(after)));
        auto& rotation = *static_cast<math::Quat*>(other);
        math::Vec3 degrees = math::degrees(math::eulerAngles(rotation));
        for (int index = 0; index < 3; ++index)
        {
            if (std::abs(from[index] - to[index]) > 1e-4f)
            {
                degrees[index] = to[index];
            }
        }
        rotation = math::quatFromEulerAngles(math::radians(degrees));
        break;
    }
    default:
        break;
    }
}

[[nodiscard]] bool isVectorField(const reflection::FieldInfo& field) noexcept
{
    return !field.color && (field.kind == ValueKind::Vec2 || field.kind == ValueKind::Vec3 ||
                            field.kind == ValueKind::Vec4 || field.kind == ValueKind::Quat);
}

// A field of a component every selected entity has. It shows the value of the active entity, or a
// dash where the others differ; a change goes to all of them, one undo step for the whole edit.
void drawSharedField(ToolsState& state, scene::Scene& scene, const std::vector<scene::Entity>& entities,
                     const scene::ComponentType& type, const reflection::FieldInfo& field)
{
    const scene::Entity active = entities.back();
    void* const address = field.address(const_cast<void*>(type.find(scene, active)));
    const std::string label = displayName(field.name);
    propertyName(label.c_str());
    if (field.list != nullptr || field.kind == ValueKind::Uuid)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", field.list != nullptr ? "One entity at a time" : mixedValue);
        return;
    }
    // The values before this frame's change.
    std::vector<serialization::TextValue> before;
    std::vector<const void*> others;
    for (const scene::Entity entity : entities)
    {
        const void* const value = field.address(type.find(scene, entity));
        before.push_back(scene::writeFieldValue(field, value));
        if (entity != active)
        {
            others.push_back(value);
        }
    }
    const bool mixed = std::ranges::any_of(before, [&](const serialization::TextValue& value) { return value != before.back(); });
    const unsigned components = isVectorField(field) ? mixedComponents(field, address, others) : 0;

    const std::string id = "##" + std::string(field.name);
    const bool changed = drawValueWidget(state, scene, id.c_str(), field, address, mixed && !isVectorField(field), components);
    if (ImGui::IsItemActivated())
    {
        state.fieldEditStarts.clear();
        for (std::size_t index = 0; index < entities.size(); ++index)
        {
            state.fieldEditStarts.emplace_back(scene.uuid(entities[index]), before[index]);
        }
    }
    if (changed)
    {
        // The active entity took the change; the others follow, a vector only in the components
        // that changed.
        alignas(16) std::array<std::byte, 64> previous{};
        const bool vector = isVectorField(field);
        if (vector)
        {
            static_cast<void>(scene::readFieldValue(field, before.back(), previous.data()));
        }
        const serialization::TextValue after = scene::writeFieldValue(field, address);
        for (const scene::Entity entity : entities)
        {
            if (entity == active)
            {
                continue;
            }
            void* const other = field.address(const_cast<void*>(type.find(scene, entity)));
            if (vector)
            {
                copyChangedComponents(field, previous.data(), address, other);
            }
            else
            {
                static_cast<void>(scene::readFieldValue(field, after, other));
            }
        }
    }
    const bool oneClick = isOneClickEdit(field);
    if (ImGui::IsItemDeactivatedAfterEdit() || (changed && oneClick))
    {
        std::vector<std::unique_ptr<Command>> commands;
        for (std::size_t index = 0; index < entities.size(); ++index)
        {
            const core::Uuid uuid = scene.uuid(entities[index]);
            serialization::TextValue start = before[index];
            if (!oneClick)
            {
                const auto found = std::ranges::find(state.fieldEditStarts, uuid, &std::pair<core::Uuid, serialization::TextValue>::first);
                if (found != state.fieldEditStarts.end())
                {
                    start = found->second;
                }
            }
            serialization::TextValue end = scene::writeFieldValue(field, field.address(type.find(scene, entities[index])));
            if (start != end)
            {
                commands.push_back(makeSetFieldCommand(uuid, std::string(type.name()), field.name, std::move(start), std::move(end)));
            }
        }
        const std::size_t count = commands.size();
        if (count == 1)
        {
            state.history.recordApplied(std::move(commands.front()));
        }
        else if (count > 1)
        {
            state.history.recordApplied(makeCompositeCommand(std::move(commands), std::format("Edit {} of {} entities", label, count)));
        }
        state.fieldEditStarts.clear();
    }
}

// The inspector of several entities: the components they all have, and components to add to all.
void drawSharedInspector(ToolsState& state, scene::Scene& scene, const std::vector<scene::Entity>& entities)
{
    const scene::Entity active = entities.back();
    const EntityIcon icon = entityIcon(scene, active);
    ImGui::AlignTextToFramePadding();
    iconLabel(icon.icon, icon.color);
    boldText(std::format("{} entities", entities.size()).c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("A dash marks values that differ; changes apply to all of them.");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    for (const scene::ComponentType& type : scene::componentRegistry().types())
    {
        if (!std::ranges::all_of(entities, [&](scene::Entity entity) { return type.find(scene, entity) != nullptr; }))
        {
            continue;
        }
        const std::string name(type.name());
        ImGui::PushID(name.c_str());
        bool removed = false;
        const bool fromPrefab = std::ranges::any_of(entities, [&](scene::Entity entity) { return hasFromPrefab(scene, entity, type); });
        if (componentHeader(name.c_str(), componentIcon(name), true, &removed, fromPrefab) && beginProperties("fields"))
        {
            for (const reflection::FieldInfo& field : type.type->fields)
            {
                drawSharedField(state, scene, entities, type, field);
            }
            endProperties();
        }
        if (removed)
        {
            std::vector<std::unique_ptr<Command>> commands;
            for (const scene::Entity entity : entities)
            {
                commands.push_back(makeRemoveComponentCommand(scene.uuid(entity), name));
            }
            state.pendingCommand = makeCompositeCommand(std::move(commands), std::format("Remove {} from {} entities", name, entities.size()));
        }
        ImGui::Spacing();
        ImGui::PopID();
    }

    // Components that some of them lack, added to those.
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
        for (const scene::ComponentType& type : scene::componentRegistry().types())
        {
            const std::string name(type.name());
            std::vector<core::Uuid> lacking;
            for (const scene::Entity entity : entities)
            {
                if (type.find(scene, entity) == nullptr)
                {
                    lacking.push_back(scene.uuid(entity));
                }
            }
            if (lacking.empty() || !containsIgnoringCase(name, filter))
            {
                continue;
            }
            const EntityIcon componentIconOf = componentIcon(name);
            const ImVec2 position = ImGui::GetCursorScreenPos();
            const std::string label = std::format("      {}", name);
            if (ImGui::Selectable(label.c_str()))
            {
                std::vector<std::unique_ptr<Command>> commands;
                for (const core::Uuid uuid : lacking)
                {
                    commands.push_back(makeAddComponentCommand(uuid, name));
                }
                state.pendingCommand = makeCompositeCommand(std::move(commands), std::format("Add {} to {} entities", name, lacking.size()));
            }
            if (lacking.size() < entities.size())
            {
                ImGui::SetItemTooltip("Added to the %zu that lack it", lacking.size());
            }
            ImGui::GetWindowDrawList()->AddText(position, uiColorU32(componentIconOf.color), componentIconOf.icon.c_str());
        }
        ImGui::EndPopup();
    }
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
        if (state.mode == ToolsMode::Editor && state.database != nullptr)
        {
            const ImVec2 position = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("      New Script..."))
            {
                state.openNewScriptPopup = true;
            }
            ImGui::SetItemTooltip("Writes a component in a new file of the code folder and adds it here");
            ImGui::GetWindowDrawList()->AddText(position, uiColorU32(themeColors().gameCode), icons::FilePlus.c_str());
            ImGui::Separator();
        }
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

// Under the style field of an element: what its style does, or why it does nothing, and a way
// to the theme that holds it.
void drawStyleStatus(ToolsState& state, const ui::ElementStyle& style)
{
    if (style.name.empty())
    {
        return;
    }
    const ThemeColors& colors = themeColors();
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(1);

    std::optional<std::filesystem::path> themeFile;
    std::string themeName = "the theme";
    if (state.database != nullptr && style.theme.isValid())
    {
        if (const std::optional<asset::SourceFile> source = state.database->sourceOf(style.theme))
        {
            themeFile = state.database->project().absolutePath(source->path);
            const std::size_t slash = source->path.find_last_of('/');
            themeName = slash == std::string::npos ? source->path : source->path.substr(slash + 1);
        }
    }

    ImGui::AlignTextToFramePadding();
    if (!style.theme.isValid())
    {
        iconLabel(icons::TriangleAlert, colors.warning);
        ImGui::SameLine();
        ImGui::TextColored(uiColor(colors.warning), "No theme on the canvas");
        ImGui::SetItemTooltip("The canvas above this element names no theme, so the style '%s' sets nothing.",
                              style.name.c_str());
        return;
    }
    if (style.data == nullptr || style.style == nullptr)
    {
        iconLabel(icons::TriangleAlert, colors.warning);
        ImGui::SameLine();
        ImGui::TextColored(uiColor(colors.warning), "Not in the theme");
        if (style.data == nullptr)
        {
            ImGui::SetItemTooltip("The theme of the canvas, %s, is not loaded.", themeName.c_str());
        }
        else
        {
            ImGui::SetItemTooltip("%s has no style named '%s'.", themeName.c_str(), style.name.c_str());
        }
    }
    else
    {
        // Short, so that the way to the theme stays in view in a narrow panel; the name of the
        // theme is in the tooltip.
        iconLabel(icons::Palette, colors.accent);
        ImGui::SameLine();
        const std::size_t count = style.style->values.size();
        ImGui::TextDisabled("%zu %s", count, count == 1 ? "field" : "fields");
        ImGui::SetItemTooltip("The style '%s' of %s sets %zu %s of this element: they show greyed out.",
                              style.name.c_str(), themeName.c_str(), count, count == 1 ? "field" : "fields");
    }
    if (themeFile)
    {
        ImGui::SameLine();
        if (ImGui::SmallButton("Open"))
        {
            openTextFile(state, *themeFile);
        }
        ImGui::SetItemTooltip("Open %s in the Script screen", themeName.c_str());
    }
}

void drawInspectorPanel(ToolsState& state, scene::Scene& scene)
{
    if (ImGui::Begin(inspectorWindow))
    {
        const scene::Entity entity = scene.findEntity(state.selection.active());
        if (entity.isValid())
        {
            state.selectedCode.clear();
            state.selectedAsset = {};
        }
        // Several entities: what they share.
        if (state.selection.size() > 1)
        {
            std::vector<scene::Entity> entities;
            for (const core::Uuid selected : state.selection.entities())
            {
                if (const scene::Entity found = scene.findEntity(selected); found.isValid())
                {
                    entities.push_back(found);
                }
            }
            if (entities.size() > 1)
            {
                drawSharedInspector(state, scene, entities);
                ImGui::End();
                return;
            }
        }
        // A clip plays while its inspector shows.
        if (state.previewedClip.isValid() && state.previewedClip != state.selectedAsset)
        {
            stopAudioPreview(state);
        }
        if (!entity.isValid() && !state.selectedCode.empty())
        {
            drawCodeInspector(state);
            ImGui::End();
            return;
        }
        if (!entity.isValid() && state.selectedAsset.isValid())
        {
            drawAudioClipInspector(state);
            ImGui::End();
            return;
        }
        if (!entity.isValid())
        {
            const char* const hint = "Select an entity to inspect it.";
            const ImVec2 size = ImGui::CalcTextSize(hint);
            ImGui::SetCursorPos(ImVec2(std::max(0.0f, (ImGui::GetWindowWidth() - size.x) * 0.5f), ImGui::GetWindowHeight() * 0.35f));
            ImGui::TextDisabled("%s", hint);
            ImGui::End();
            return;
        }

        const core::Uuid uuid = state.selection.active();

        // The entity as its prefab makes it, which overridden values differ from.
        const scene::Entity instance = scene::owningPrefabInstance(scene, entity);
        std::shared_ptr<const scene::Scene> prefabBase;
        scene::Entity prefabEntity;
        if (instance.isValid() && scene.has<scene::PrefabEntity>(entity) && scene.get<scene::PrefabInstance>(instance).resolved)
        {
            prefabBase = scene::prefabBase(scene.get<scene::PrefabInstance>(instance).prefab, scene.uuid(instance));
            prefabEntity = prefabBase != nullptr ? prefabBase->findEntity(uuid) : scene::Entity{};
        }

        const EntityIcon icon = entityIcon(scene, entity);
        ImGui::AlignTextToFramePadding();
        iconLabel(icon.icon, icon.color);
        drawNameField(state, scene, entity, uuid,
                      prefabEntity.isValid() && entity != instance ? &prefabBase->name(prefabEntity) : nullptr);
        const std::string uuidText = uuid.toString();
        ImGui::TextDisabled("%s", uuidText.c_str());
        ImGui::SetItemTooltip("The UUID of the entity, which scenes and undo steps refer to");
        ImGui::Spacing();
        if (instance.isValid())
        {
            drawPrefabSection(state, scene, entity, instance);
        }
        const ui::ElementStyle style = ui::styleOf(scene, entity, state.themes);

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
            const void* const prefabComponent = prefabEntity.isValid() ? type.find(*prefabBase, prefabEntity) : nullptr;
            if (componentHeader(name.c_str(), componentIcon(name), true, &removed, prefabComponent != nullptr,
                                prefabEntity.isValid() && prefabComponent == nullptr) &&
                beginProperties("fields"))
            {
                for (const reflection::FieldInfo& field : type.type->fields)
                {
                    drawField(state, scene, uuid, type, field, const_cast<void*>(component), prefabComponent,
                              style);
                    if (name == "UiRect" && field.name == "style")
                    {
                        drawStyleStatus(state, style);
                    }
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
