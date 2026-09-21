#include "ToolsState.hpp"

#include <devex/scene/Prefab.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <optional>

namespace devex::tools::detail {
namespace {

using scene::Entity;

// Accepts an entity dropped on the last item and returns its UUID.
[[nodiscard]] std::optional<core::Uuid> acceptDroppedEntity()
{
    std::optional<core::Uuid> dropped;
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* const payload = ImGui::AcceptDragDropPayload(entityPayload))
        {
            std::array<std::uint8_t, 16> bytes{};
            std::memcpy(bytes.data(), payload->Data, bytes.size());
            dropped = uuidFromBytes(bytes);
        }
        ImGui::EndDragDropTarget();
    }
    return dropped;
}

[[nodiscard]] bool containsIgnoringCase(std::string_view text, std::string_view part)
{
    return part.empty() || !std::ranges::search(text, part, [](char left, char right) {
                                return std::tolower(static_cast<unsigned char>(left)) ==
                                       std::tolower(static_cast<unsigned char>(right));
                            }).empty();
}

// Whether the entity or one of its descendants matches the filter.
[[nodiscard]] bool matchesFilter(const scene::Scene& scene, Entity entity, std::string_view filter)
{
    if (containsIgnoringCase(scene.name(entity), filter))
    {
        return true;
    }
    for (Entity child = scene.firstChild(entity); child.isValid(); child = scene.nextSibling(child))
    {
        if (matchesFilter(scene, child, filter))
        {
            return true;
        }
    }
    return false;
}

void drawEntity(ToolsState& state, scene::Scene& scene, Entity entity)
{
    const std::string_view filter = state.hierarchyFilter;
    if (!filter.empty() && !matchesFilter(scene, entity, filter))
    {
        return;
    }
    const core::Uuid uuid = scene.uuid(entity);
    const std::string& name = scene.name(entity);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding |
                               ImGuiTreeNodeFlags_DefaultOpen;
    if (!scene.firstChild(entity).isValid())
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (uuid == state.selection)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (!filter.empty())
    {
        ImGui::SetNextItemOpen(true);
    }

    ImGui::PushID(static_cast<int>(entity.index));
    const float nodeX = ImGui::GetCursorScreenPos().x;
    const bool open = iconTreeNode("##entity", flags);
    const ImVec2 rowMin = ImGui::GetItemRectMin();
    const float rowHeight = ImGui::GetItemRectSize().y;

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
    {
        state.selection = uuid;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && state.mode == ToolsMode::Editor)
    {
        frameSelection(state, scene);
    }
    // The entities of a prefab instance stay where their prefab puts them.
    const bool fromPrefab = scene::isInsidePrefabInstance(scene, entity);
    if (!fromPrefab && ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload(entityPayload, uuid.bytes().data(), uuid.bytes().size());
        const EntityIcon icon = entityIcon(scene, entity);
        iconLabel(icon.icon, icon.color);
        ImGui::TextUnformatted(name.c_str());
        ImGui::EndDragDropSource();
    }
    if (const std::optional<core::Uuid> dropped = acceptDroppedEntity(); dropped && *dropped != uuid)
    {
        state.pendingCommand = makeReparentCommand(*dropped, uuid);
    }
    if (const std::optional<asset::AssetId> model = acceptDroppedAsset(asset::AssetType::Model))
    {
        requestInstantiateModel(state, *model, uuid);
    }
    if (const std::optional<asset::AssetId> prefab = acceptDroppedAsset(asset::AssetType::Scene))
    {
        requestInstantiatePrefab(state, *prefab, uuid);
    }
    if (ImGui::BeginPopupContextItem("entity menu"))
    {
        state.selection = uuid;
        if (ImGui::BeginMenuEx("Create Child", icons::Plus.c_str()))
        {
            drawCreateEntityMenu(state, uuid);
            ImGui::EndMenu();
        }
        if (state.mode == ToolsMode::Editor && ImGui::MenuItemEx("Frame", icons::Crosshair.c_str(), "F"))
        {
            frameSelection(state, scene);
        }
        if (state.mode == ToolsMode::Editor)
        {
            const bool editing = state.playState == PlayState::Editing;
            ImGui::Separator();
            const Entity instance = scene::owningPrefabInstance(scene, entity);
            if (instance.isValid())
            {
                const scene::PrefabInstance& prefab = scene.get<scene::PrefabInstance>(instance);
                if (ImGui::MenuItemEx("Open Prefab", icons::ExternalLink.c_str(), nullptr, false, editing))
                {
                    state.prefabToOpen = prefab.prefab;
                }
                if (ImGui::MenuItemEx("Make Local", icons::Unlink.c_str(), nullptr, false, editing && prefab.resolved))
                {
                    state.pendingCommand = makeReplaceEntityTreeCommand(
                        scene.uuid(instance), scene::saveUnpackedEntityTree(scene, instance), "Make instance local");
                }
            }
            if (ImGui::MenuItemEx("Save as Prefab...", icons::Package.c_str(), nullptr, false,
                                  editing && !fromPrefab && state.database != nullptr))
            {
                showSaveAsPrefabDialog(state, scene, uuid);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItemEx("Delete", icons::Trash.c_str(), "Delete", false, !fromPrefab))
        {
            state.pendingCommand = makeDestroyEntityCommand(uuid);
        }
        ImGui::EndPopup();
    }

    // The icon and the name, drawn over the node so that it stays the item for clicks and drags.
    const EntityIcon icon = entityIcon(scene, entity);
    const float textY = rowMin.y + (rowHeight - ImGui::GetFontSize()) * 0.5f;
    const float iconX = nodeX + ImGui::GetTreeNodeToLabelSpacing();
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    draw->AddText(ImVec2(iconX, textY), uiColorU32(icon.color), icon.icon.c_str());
    const float nameX = iconX + ImGui::CalcTextSize(icon.icon.c_str()).x + ImGui::GetStyle().ItemInnerSpacing.x;
    // Entities from prefabs are named in the prefab color, and instances whose prefab is missing in red.
    ImU32 nameColor = ImGui::GetColorU32(name.empty() ? ImGuiCol_TextDisabled : ImGuiCol_Text);
    if (const scene::PrefabInstance* const instance = scene.tryGet<scene::PrefabInstance>(entity);
        instance != nullptr && !instance->resolved)
    {
        nameColor = uiColorU32(themeColors().error);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("The prefab of this instance is missing");
        }
    }
    else if (scene.has<scene::PrefabEntity>(entity))
    {
        nameColor = uiColorU32(themeColors().prefab);
    }
    draw->AddText(ImVec2(nameX, textY), nameColor, name.empty() ? "(unnamed)" : name.c_str());

    if (open)
    {
        for (Entity child = scene.firstChild(entity); child.isValid(); child = scene.nextSibling(child))
        {
            drawEntity(state, scene, child);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

} // namespace

void drawHierarchyPanel(ToolsState& state, scene::Scene& scene)
{
    if (ImGui::Begin(hierarchyWindow))
    {
        if (toolButton("add", icons::Plus, "Create an entity"))
        {
            ImGui::OpenPopup("create entity");
        }
        if (ImGui::BeginPopup("create entity"))
        {
            drawCreateEntityMenu(state, core::Uuid{});
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        searchField("##filter", state.hierarchyFilter, "Filter Entities");

        ImGui::PushStyleColor(ImGuiCol_ChildBg, uiColor(themeColors().field));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::GetStyle().FrameRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
        if (ImGui::BeginChild("tree", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
            for (Entity root = scene.firstRoot(); root.isValid(); root = scene.nextSibling(root))
            {
                drawEntity(state, scene, root);
            }
            ImGui::PopStyleVar();

            // The empty space below the tree accepts entities to make them roots.
            ImVec2 remaining = ImGui::GetContentRegionAvail();
            remaining.y = std::max(remaining.y, ImGui::GetTextLineHeight());
            ImGui::InvisibleButton("roots", remaining);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                state.selection = core::Uuid{};
            }
            if (const std::optional<core::Uuid> dropped = acceptDroppedEntity())
            {
                state.pendingCommand = makeReparentCommand(*dropped, core::Uuid{});
            }
            if (const std::optional<asset::AssetId> model = acceptDroppedAsset(asset::AssetType::Model))
            {
                requestInstantiateModel(state, *model, core::Uuid{});
            }
            if (const std::optional<asset::AssetId> prefab = acceptDroppedAsset(asset::AssetType::Scene))
            {
                requestInstantiatePrefab(state, *prefab, core::Uuid{});
            }
            if (ImGui::BeginPopupContextItem("roots menu"))
            {
                drawCreateEntityMenu(state, core::Uuid{});
                ImGui::EndPopup();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::GetIO().WantTextInput &&
            scene.findEntity(state.selection).isValid() &&
            !scene::isInsidePrefabInstance(scene, scene.findEntity(state.selection)))
        {
            state.pendingCommand = makeDestroyEntityCommand(state.selection);
        }
    }
    ImGui::End();
}

} // namespace devex::tools::detail
