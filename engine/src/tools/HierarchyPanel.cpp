#include "ToolsState.hpp"

#include <devex/scene/Prefab.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <format>
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

// Moves the dropped entity under the new parent (nil for the roots); a dropped entity that is
// selected brings the rest of the selection with it. The entities of prefab instances stay, and
// nothing moves under itself.
void moveDropped(ToolsState& state, scene::Scene& scene, core::Uuid dropped, core::Uuid newParent)
{
    const Entity target = scene.findEntity(newParent);
    std::vector<Entity> moved;
    if (state.selection.contains(dropped))
    {
        moved = selectedRoots(scene, state.selection);
    }
    else if (const Entity entity = scene.findEntity(dropped); entity.isValid())
    {
        moved.push_back(entity);
    }
    std::vector<std::unique_ptr<Command>> commands;
    for (const Entity entity : moved)
    {
        bool underItself = false;
        for (Entity ancestor = target; ancestor.isValid(); ancestor = scene.parent(ancestor))
        {
            underItself |= ancestor == entity;
        }
        if (!underItself && !scene::isInsidePrefabInstance(scene, entity) && scene.parent(entity) != target)
        {
            commands.push_back(makeReparentCommand(scene.uuid(entity), newParent));
        }
    }
    if (commands.size() == 1)
    {
        state.pendingCommand = std::move(commands.front());
    }
    else if (!commands.empty())
    {
        const std::size_t count = commands.size();
        state.pendingCommand = makeCompositeCommand(std::move(commands), std::format("Move {} entities", count));
    }
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

// Selects as a click in the tree does: Ctrl adds or removes, Shift selects the rows between the
// anchor and this one, as the tree listed them.
void clickRow(ToolsState& state, core::Uuid uuid)
{
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyShift && !state.rangeAnchor.isNil())
    {
        const auto from = std::ranges::find(state.hierarchyOrder, state.rangeAnchor);
        const auto to = std::ranges::find(state.hierarchyOrder, uuid);
        if (from != state.hierarchyOrder.end() && to != state.hierarchyOrder.end())
        {
            std::vector<core::Uuid> range(std::min(from, to), std::max(from, to) + 1);
            // The clicked row ends active.
            std::erase(range, uuid);
            range.push_back(uuid);
            if (!io.KeyCtrl)
            {
                state.selection.clear();
            }
            for (const core::Uuid entity : range)
            {
                state.selection.add(entity);
            }
            return;
        }
    }
    if (io.KeyCtrl)
    {
        state.selection.toggle(uuid);
    }
    else
    {
        state.selection.set(uuid);
    }
    state.rangeAnchor = uuid;
}

void drawEntityMenu(ToolsState& state, scene::Scene& scene, Entity entity, core::Uuid uuid)
{
    const bool single = state.selection.size() <= 1;
    const bool editor = state.mode == ToolsMode::Editor;
    if (ImGui::BeginMenuEx("Create Child", icons::Plus.c_str(), single))
    {
        drawCreateEntityMenu(state, uuid);
        ImGui::EndMenu();
    }
    if (editor && ImGui::MenuItemEx("Frame", icons::Crosshair.c_str(), "F"))
    {
        frameSelection(state, scene);
    }
    if (ImGui::MenuItemEx("Rename", icons::Pencil.c_str(), "F2"))
    {
        startRename(state, uuid);
    }
    const bool hidden = std::ranges::all_of(state.selection.entities(), [&state](core::Uuid selected) {
        return state.hiddenEntities.contains(selected);
    });
    if (editor && ImGui::MenuItemEx(hidden ? "Show in Viewport" : "Hide in Viewport",
                                    (hidden ? icons::Eye : icons::EyeOff).c_str(), "H"))
    {
        toggleSelectionHidden(state, scene);
    }
    ImGui::Separator();
    const bool deletable = canDeleteSelection(state, scene);
    if (ImGui::MenuItemEx("Cut", icons::Scissors.c_str(), "Ctrl+X", false, deletable))
    {
        cutSelection(state, scene);
    }
    if (ImGui::MenuItemEx("Copy", icons::Copy.c_str(), "Ctrl+C"))
    {
        copySelection(state, scene);
    }
    if (ImGui::MenuItemEx("Paste", icons::ClipboardPaste.c_str(), "Ctrl+V"))
    {
        pasteEntities(state, scene);
    }
    if (ImGui::MenuItemEx("Duplicate", icons::CopyPlus.c_str(), "Ctrl+D"))
    {
        duplicateSelection(state, scene);
    }
    if (editor)
    {
        const bool editing = state.playState == PlayState::Editing;
        const bool fromPrefab = scene::isInsidePrefabInstance(scene, entity);
        ImGui::Separator();
        const Entity instance = scene::owningPrefabInstance(scene, entity);
        if (instance.isValid() && single)
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
                              single && editing && !fromPrefab && state.database != nullptr))
        {
            showSaveAsPrefabDialog(state, scene, uuid);
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItemEx("Delete", icons::Trash.c_str(), "Delete", false, deletable))
    {
        deleteSelection(state, scene);
    }
}

// The name typed in place of the entity's, applied on Enter or when the field loses the keyboard,
// and dropped on Escape.
void drawRenameField(ToolsState& state, const scene::Scene& scene, core::Uuid uuid, ImVec2 position, float width)
{
    const ImVec2 restore = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(position);
    ImGui::SetNextItemWidth(width);
    if (state.focusRename)
    {
        state.renameBuffer = scene.name(scene.findEntity(uuid));
        ImGui::SetKeyboardFocusHere();
        ImGui::SetScrollHereY();
        state.focusRename = false;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0.0f));
    ImGui::InputText("##rename", &state.renameBuffer, ImGuiInputTextFlags_AutoSelectAll);
    ImGui::PopStyleVar();
    if (ImGui::IsItemDeactivated())
    {
        const std::string& before = scene.name(scene.findEntity(uuid));
        if (!ImGui::IsKeyPressed(ImGuiKey_Escape) && !state.renameBuffer.empty() && state.renameBuffer != before)
        {
            state.pendingCommand = makeRenameCommand(uuid, before, state.renameBuffer);
        }
        state.renamedEntity = {};
    }
    ImGui::SetCursorScreenPos(restore);
}

void drawEntity(ToolsState& state, scene::Scene& scene, Entity entity, bool ancestorHidden)
{
    const std::string_view filter = state.hierarchyFilter;
    if (!filter.empty() && !matchesFilter(scene, entity, filter))
    {
        return;
    }
    const core::Uuid uuid = scene.uuid(entity);
    const std::string& name = scene.name(entity);
    const bool hidden = ancestorHidden || state.hiddenEntities.contains(uuid);
    state.hierarchyOrder.push_back(uuid);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth |
                               ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_DefaultOpen |
                               ImGuiTreeNodeFlags_AllowOverlap;
    if (!scene.firstChild(entity).isValid())
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (state.selection.contains(uuid))
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
    const ImVec2 rowMax = ImGui::GetItemRectMax();
    const float rowHeight = ImGui::GetItemRectSize().y;
    const bool rowHovered = ImGui::IsItemHovered();

    // A click on a selected row keeps the others selected until the button is released without a
    // drag, so that the whole selection can be dragged.
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (state.selection.contains(uuid) && state.selection.size() > 1 && !io.KeyCtrl && !io.KeyShift)
        {
            state.pendingRowClick = uuid;
        }
        else
        {
            clickRow(state, uuid);
        }
    }
    if (state.pendingRowClick == uuid && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        if (rowHovered)
        {
            clickRow(state, uuid);
        }
        state.pendingRowClick = {};
    }
    if (rowHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && state.mode == ToolsMode::Editor)
    {
        frameSelection(state, scene);
    }
    // The entities of a prefab instance stay where their prefab puts them.
    const bool fromPrefab = scene::isInsidePrefabInstance(scene, entity);
    if (!fromPrefab && ImGui::BeginDragDropSource())
    {
        state.pendingRowClick = {};
        ImGui::SetDragDropPayload(entityPayload, uuid.bytes().data(), uuid.bytes().size());
        const EntityIcon icon = entityIcon(scene, entity);
        iconLabel(icon.icon, icon.color);
        if (state.selection.contains(uuid) && state.selection.size() > 1)
        {
            ImGui::Text("%zu entities", state.selection.size());
        }
        else
        {
            ImGui::TextUnformatted(name.c_str());
        }
        ImGui::EndDragDropSource();
    }
    if (const std::optional<core::Uuid> dropped = acceptDroppedEntity(); dropped && *dropped != uuid)
    {
        moveDropped(state, scene, *dropped, uuid);
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
        if (ImGui::IsWindowAppearing() && !state.selection.contains(uuid))
        {
            state.selection.set(uuid);
            state.rangeAnchor = uuid;
        }
        drawEntityMenu(state, scene, entity, uuid);
        ImGui::EndPopup();
    }

    // The eye at the end of the row: shown on hover, and always while the entity is hidden.
    const ThemeColors& colors = themeColors();
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    const float eyeWidth = ImGui::CalcTextSize(icons::Eye.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float textY = rowMin.y + (rowHeight - ImGui::GetFontSize()) * 0.5f;
    if (state.mode == ToolsMode::Editor && (rowHovered || hidden))
    {
        const ImVec2 restore = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(rowMax.x - eyeWidth, rowMin.y));
        if (ImGui::InvisibleButton("eye", ImVec2(eyeWidth, rowHeight)))
        {
            if (!state.hiddenEntities.erase(uuid))
            {
                state.hiddenEntities.insert(uuid);
            }
            saveEditorSettings(state);
        }
        const bool eyeHovered = ImGui::IsItemHovered();
        ImGui::SetItemTooltip("%s", state.hiddenEntities.contains(uuid) ? "Show in the viewport (H)"
                                    : ancestorHidden                     ? "Hidden with an entity above it"
                                                                         : "Hide in the viewport (H)");
        const IconText eye = hidden ? icons::EyeOff : icons::Eye;
        draw->AddText(ImVec2(rowMax.x - eyeWidth + ImGui::GetStyle().FramePadding.x, textY),
                      uiColorU32(eyeHovered ? colors.text : colors.textDim), eye.c_str());
        ImGui::SetCursorScreenPos(restore);
    }

    // The icon and the name, drawn over the node so that it stays the item for clicks and drags.
    const EntityIcon icon = entityIcon(scene, entity);
    const float iconX = nodeX + ImGui::GetTreeNodeToLabelSpacing();
    draw->AddText(ImVec2(iconX, textY), uiColorU32(hidden ? colors.textDim : icon.color), icon.icon.c_str());
    const float nameX = iconX + ImGui::CalcTextSize(icon.icon.c_str()).x + ImGui::GetStyle().ItemInnerSpacing.x;
    if (state.renamedEntity == uuid)
    {
        drawRenameField(state, scene, uuid, ImVec2(nameX, rowMin.y), std::max(rowMax.x - eyeWidth - nameX, 40.0f));
    }
    else
    {
        // Entities from prefabs are named in the prefab color, and instances whose prefab is missing in red.
        ImU32 nameColor = ImGui::GetColorU32(name.empty() || hidden ? ImGuiCol_TextDisabled : ImGuiCol_Text);
        if (const scene::PrefabInstance* const instance = scene.tryGet<scene::PrefabInstance>(entity);
            instance != nullptr && !instance->resolved)
        {
            nameColor = uiColorU32(colors.error);
            if (rowHovered)
            {
                ImGui::SetTooltip("The prefab of this instance is missing");
            }
        }
        else if (scene.has<scene::PrefabEntity>(entity))
        {
            nameColor = uiColorU32(hidden ? colors.textDim : colors.prefab);
        }
        draw->AddText(ImVec2(nameX, textY), nameColor, name.empty() ? "(unnamed)" : name.c_str());
    }

    if (open)
    {
        for (Entity child = scene.firstChild(entity); child.isValid(); child = scene.nextSibling(child))
        {
            drawEntity(state, scene, child, hidden);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

} // namespace

void drawHierarchyPanel(ToolsState& state, scene::Scene& scene)
{
    state.hierarchyFocused = false;
    if (ImGui::Begin(hierarchyWindow))
    {
        state.hierarchyFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
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
            state.hierarchyOrder.clear();
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
            for (Entity root = scene.firstRoot(); root.isValid(); root = scene.nextSibling(root))
            {
                drawEntity(state, scene, root, false);
            }
            ImGui::PopStyleVar();

            // The empty space below the tree accepts entities to make them roots.
            ImVec2 remaining = ImGui::GetContentRegionAvail();
            remaining.y = std::max(remaining.y, ImGui::GetTextLineHeight());
            ImGui::InvisibleButton("roots", remaining);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift)
            {
                state.selection.clear();
            }
            if (const std::optional<core::Uuid> dropped = acceptDroppedEntity())
            {
                moveDropped(state, scene, *dropped, core::Uuid{});
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
                ImGui::Separator();
                if (ImGui::MenuItemEx("Paste", icons::ClipboardPaste.c_str(), "Ctrl+V"))
                {
                    state.selection.clear();
                    pasteEntities(state, scene);
                }
                ImGui::EndPopup();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }
    ImGui::End();
}

} // namespace devex::tools::detail
