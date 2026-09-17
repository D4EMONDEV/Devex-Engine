#include "ToolsState.hpp"

#include <devex/asset/Project.hpp>
#include <devex/core/Log.hpp>

#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace devex::tools::detail {
namespace {

[[nodiscard]] bool containsIgnoringCase(std::string_view text, std::string_view part)
{
    return std::ranges::search(text, part, [](char left, char right) {
               return std::tolower(static_cast<unsigned char>(left)) ==
                      std::tolower(static_cast<unsigned char>(right));
           }).begin() != text.end() ||
           part.empty();
}

[[nodiscard]] ImVec4 statusColor(asset::ImportStatus status) noexcept
{
    switch (status)
    {
    case asset::ImportStatus::Importing:
        return linearColor({0.95f, 0.75f, 0.30f, 1.0f});
    case asset::ImportStatus::Ready:
        return linearColor({0.55f, 0.80f, 0.55f, 1.0f});
    case asset::ImportStatus::Failed:
        return linearColor({0.95f, 0.40f, 0.35f, 1.0f});
    }
    return {1.0f, 1.0f, 1.0f, 1.0f};
}

void drawSourceMenu(ToolsState& state, const asset::SourceFile& source,
                    const asset::AssetInfo* mainAsset)
{
    if (!ImGui::BeginPopupContextItem("source menu"))
    {
        return;
    }
    if (ImGui::MenuItem("Reimport"))
    {
        if (core::Result<void> queued = state.database->reimport(source.id); !queued)
        {
            DEVEX_LOG_WARNING("{}", queued.error());
        }
    }
    const bool isModel = mainAsset != nullptr && mainAsset->type == asset::AssetType::Model;
    if (ImGui::MenuItem("Place in scene", nullptr, false, isModel))
    {
        requestInstantiateModel(state, source.id, core::Uuid{});
    }
    ImGui::EndPopup();
}

void drawSource(ToolsState& state, scene::Scene& scene, const asset::SourceFile& source)
{
    const asset::AssetDatabase& database = *state.database;
    const asset::AssetInfo* const mainAsset = database.find(source.id);
    // Paths are shown relative to the assets folder.
    std::string_view label = source.path;
    if (constexpr std::string_view prefix = "res://assets/"; label.starts_with(prefix))
    {
        label.remove_prefix(prefix.size());
    }

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::PushID(source.id.uuid.toString().c_str());
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow;
    if (source.assets.size() <= 1)
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    const std::string text(label);
    const bool open = ImGui::TreeNodeEx("source", flags, "%s", text.c_str());
    if (mainAsset != nullptr)
    {
        dragAsset(mainAsset->id, mainAsset->type, mainAsset->name);
        if (mainAsset->type == asset::AssetType::Model && ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            requestInstantiateModel(state, mainAsset->id, core::Uuid{});
        }
        if (mainAsset->type == asset::AssetType::Scene && state.mode == ToolsMode::Editor && ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            if (const std::optional<std::filesystem::path> path = database.project().absolutePath(source.path))
            {
                requestSceneChange(state, scene, {SceneChange::Kind::OpenScene, *path});
            }
        }
    }
    drawSourceMenu(state, source, mainAsset);

    ImGui::TableSetColumnIndex(1);
    ImGui::TextDisabled("%s", mainAsset != nullptr
                                  ? std::string(asset::toString(mainAsset->type)).c_str()
                                  : source.importer.c_str());
    ImGui::TableSetColumnIndex(2);
    ImGui::TextColored(statusColor(source.status), "%s",
                       std::string(asset::toString(source.status)).c_str());
    if (!source.error.empty() && ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", source.error.c_str());
    }

    if (open)
    {
        for (const asset::AssetId id : source.assets)
        {
            const asset::AssetInfo* const info = database.find(id);
            if (id == source.id || info == nullptr)
            {
                continue;
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(id.uuid.toString().c_str());
            ImGui::TreeNodeEx("asset", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                           ImGuiTreeNodeFlags_SpanFullWidth,
                              "%s", info->name.c_str());
            dragAsset(info->id, info->type, info->name);
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", std::string(asset::toString(info->type)).c_str());
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

} // namespace

void drawAssetsPanel(ToolsState& state, scene::Scene& scene)
{
    if (ImGui::Begin(assetsWindow, &state.showAssets))
    {
        if (state.database == nullptr)
        {
            ImGui::TextDisabled("No project: set ApplicationConfig::project to import assets.");
            ImGui::End();
            return;
        }

        const asset::AssetDatabase& database = *state.database;
        ImGui::Text("%s", database.project().name.c_str());
        if (const std::size_t pending = database.pendingImports(); pending > 0)
        {
            ImGui::SameLine();
            ImGui::TextColored(statusColor(asset::ImportStatus::Importing), "importing %zu...",
                               pending);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##filter", "Filter", &state.assetFilter);
        ImGui::SetItemTooltip(state.mode == ToolsMode::Editor
                                  ? "Drag a model into the viewport or the hierarchy, or an asset onto an inspector "
                                    "field. Double-click a scene to open it."
                                  : "Drag a model into the hierarchy, or an asset onto an inspector field.");

        const ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                           ImGuiTableFlags_BordersInnerV |
                                           ImGuiTableFlags_Resizable;
        if (ImGui::BeginTable("assets", 3, tableFlags))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();
            for (const asset::SourceFile& source : database.sources())
            {
                if (containsIgnoringCase(source.path, state.assetFilter))
                {
                    drawSource(state, scene, source);
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

} // namespace devex::tools::detail
