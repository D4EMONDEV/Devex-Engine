#include "ToolsState.hpp"

#include <devex/tools/SceneCommands.hpp>

#include <algorithm>
#include <array>
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

void drawEntity(ToolsState& state, scene::Scene& scene, Entity entity)
{
    const core::Uuid uuid = scene.uuid(entity);
    const std::string& name = scene.name(entity);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!scene.firstChild(entity).isValid())
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (uuid == state.selection)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    ImGui::PushID(static_cast<int>(entity.index));
    const bool open =
        ImGui::TreeNodeEx("entity", flags, "%s", name.empty() ? "(unnamed)" : name.c_str());

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
    {
        state.selection = uuid;
    }
    if (ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload(entityPayload, uuid.bytes().data(), uuid.bytes().size());
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
    if (ImGui::BeginPopupContextItem("entity menu"))
    {
        state.selection = uuid;
        if (ImGui::MenuItem("Create child"))
        {
            requestCreateEntity(state, uuid);
        }
        if (ImGui::MenuItem("Delete"))
        {
            state.pendingCommand = makeDestroyEntityCommand(uuid);
        }
        ImGui::EndPopup();
    }

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
    if (ImGui::Begin(hierarchyWindow, &state.showHierarchy))
    {
        if (ImGui::Button("+ Entity"))
        {
            requestCreateEntity(state, core::Uuid{});
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%zu entities", scene.entityCount());
        ImGui::Separator();

        for (Entity root = scene.firstRoot(); root.isValid(); root = scene.nextSibling(root))
        {
            drawEntity(state, scene, root);
        }

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
        if (ImGui::BeginPopupContextItem("roots menu"))
        {
            if (ImGui::MenuItem("Create entity"))
            {
                requestCreateEntity(state, core::Uuid{});
            }
            ImGui::EndPopup();
        }

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::IsKeyPressed(ImGuiKey_Delete) && scene.findEntity(state.selection).isValid())
        {
            state.pendingCommand = makeDestroyEntityCommand(state.selection);
        }
    }
    ImGui::End();
}

} // namespace devex::tools::detail
