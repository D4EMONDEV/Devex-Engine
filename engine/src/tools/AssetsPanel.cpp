#include "ToolsState.hpp"

#include <devex/asset/Project.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <string_view>

namespace devex::tools::detail {
namespace {

// A folder of the project, built from the paths of the source files.
struct Folder
{
    std::map<std::string, Folder, std::less<>> folders;
    std::vector<const asset::SourceFile*> files;
};

[[nodiscard]] bool containsIgnoringCase(std::string_view text, std::string_view part)
{
    return part.empty() || !std::ranges::search(text, part, [](char left, char right) {
                                return std::tolower(static_cast<unsigned char>(left)) ==
                                       std::tolower(static_cast<unsigned char>(right));
                            }).empty();
}

// The folder tree points into the sources, which must outlive it.
[[nodiscard]] Folder buildTree(const std::vector<asset::SourceFile>& sources)
{
    Folder root;
    for (const asset::SourceFile& source : sources)
    {
        std::string_view path = source.path;
        if (path.starts_with(asset::resourceScheme))
        {
            path.remove_prefix(asset::resourceScheme.size());
        }
        Folder* folder = &root;
        for (std::size_t slash = path.find('/'); slash != std::string_view::npos; slash = path.find('/'))
        {
            const std::string_view name = path.substr(0, slash);
            auto found = folder->folders.find(name);
            if (found == folder->folders.end())
            {
                found = folder->folders.emplace(std::string(name), Folder{}).first;
            }
            folder = &found->second;
            path.remove_prefix(slash + 1);
        }
        folder->files.push_back(&source);
    }
    return root;
}

[[nodiscard]] std::string_view fileName(std::string_view path)
{
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

[[nodiscard]] EntityIcon assetIcon(const asset::SourceFile& source, const asset::AssetInfo* mainAsset)
{
    const ThemeColors& colors = themeColors();
    if (mainAsset == nullptr)
    {
        return {icons::File, colors.neutral};
    }
    switch (mainAsset->type)
    {
    case asset::AssetType::Scene:
        return {icons::Clapperboard, colors.scene};
    case asset::AssetType::Model:
        return {icons::Package, colors.entity};
    case asset::AssetType::Mesh:
        return {icons::Box, colors.entity};
    case asset::AssetType::Material:
        return {icons::Palette, colors.material};
    case asset::AssetType::Texture:
        return source.path.ends_with(".hdr") ? EntityIcon{icons::Mountain, colors.environment}
                                             : EntityIcon{icons::Image, colors.texture};
    }
    return {icons::File, colors.neutral};
}

void drawSourceMenu(ToolsState& state, scene::Scene& scene, const asset::SourceFile& source, const asset::AssetInfo* mainAsset)
{
    if (!ImGui::BeginPopupContextItem("source menu"))
    {
        return;
    }
    const asset::AssetDatabase& database = *state.database;
    const bool isScene = mainAsset != nullptr && mainAsset->type == asset::AssetType::Scene;
    const std::optional<std::filesystem::path> path = database.project().absolutePath(source.path);
    if (isScene && state.mode == ToolsMode::Editor && ImGui::MenuItemEx("Open Scene", icons::FolderOpen.c_str()) && path)
    {
        openSceneTab(state, scene, *path);
    }
    const bool isModel = mainAsset != nullptr && mainAsset->type == asset::AssetType::Model;
    if (isModel && ImGui::MenuItemEx("Place in Scene", icons::Plus.c_str()))
    {
        requestInstantiateModel(state, source.id, core::Uuid{});
    }
    if (isScene &&
        ImGui::MenuItemEx("Set as Startup Scene", icons::House.c_str(), nullptr, database.project().startupScene == source.path))
    {
        asset::Project project = database.project();
        project.startupScene = source.path;
        if (core::Result<void> saved = state.database->updateProject(project); !saved)
        {
            DEVEX_LOG_ERROR("Cannot save the project: {}", saved.error());
        }
        else
        {
            DEVEX_LOG_INFO("{} is the startup scene", source.path);
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItemEx("Reimport", icons::Refresh.c_str()))
    {
        if (core::Result<void> queued = state.database->reimport(source.id); !queued)
        {
            DEVEX_LOG_WARNING("{}", queued.error());
        }
    }
    if (ImGui::MenuItemEx("Copy Path", icons::Copy.c_str()))
    {
        ImGui::SetClipboardText(source.path.c_str());
    }
    if (ImGui::MenuItemEx("Show in File Manager", icons::FolderOpen.c_str(), nullptr, false, path.has_value()) && path)
    {
        if (core::Result<void> opened = state.platform.openPath(path->parent_path()); !opened)
        {
            DEVEX_LOG_WARNING("{}", opened.error());
        }
    }
    ImGui::EndPopup();
}

// A row of the tree: a node with an icon and a label drawn over it, and a status at the right.
[[nodiscard]] bool treeRow(const char* id, ImGuiTreeNodeFlags flags, EntityIcon icon, std::string_view label,
                           const char* detail = nullptr)
{
    const float nodeX = ImGui::GetCursorScreenPos().x;
    const bool open = ImGui::TreeNodeEx(id, flags | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding);
    const ImVec2 min = ImGui::GetItemRectMin();
    const float height = ImGui::GetItemRectSize().y;
    const float textY = min.y + (height - ImGui::GetFontSize()) * 0.5f;
    const float iconX = nodeX + ImGui::GetTreeNodeToLabelSpacing();
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    draw->AddText(ImVec2(iconX, textY), uiColorU32(icon.color), icon.icon.c_str());
    const float labelX = iconX + ImGui::CalcTextSize(icon.icon.c_str()).x + ImGui::GetStyle().ItemInnerSpacing.x;
    draw->AddText(ImVec2(labelX, textY), ImGui::GetColorU32(ImGuiCol_Text), label.data(), label.data() + label.size());
    if (detail != nullptr)
    {
        const float detailX = labelX + ImGui::CalcTextSize(label.data(), label.data() + label.size()).x +
                              ImGui::GetStyle().ItemSpacing.x;
        draw->AddText(ImVec2(detailX, textY), ImGui::GetColorU32(ImGuiCol_TextDisabled), detail);
    }
    return open;
}

void drawSource(ToolsState& state, scene::Scene& scene, const asset::SourceFile& source, bool showFolder)
{
    const asset::AssetDatabase& database = *state.database;
    const ThemeColors& colors = themeColors();
    const asset::AssetInfo* const mainAsset = database.find(source.id);
    ImGui::PushID(source.id.uuid.toString().c_str());

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
    std::size_t children = 0;
    for (const asset::AssetId id : source.assets)
    {
        children += id != source.id && database.find(id) != nullptr ? 1 : 0;
    }
    if (children == 0)
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    const bool startup = mainAsset != nullptr && mainAsset->type == asset::AssetType::Scene &&
                         database.project().startupScene == source.path;
    std::string detail = startup ? "startup" : "";
    std::string_view label = fileName(source.path);
    std::string folderLabel;
    if (showFolder)
    {
        folderLabel = source.path;
        label = folderLabel;
    }
    const bool open = treeRow("##source", flags, assetIcon(source, mainAsset), label, detail.empty() ? nullptr : detail.c_str());
    const ImVec2 rowMin = ImGui::GetItemRectMin();
    const ImVec2 rowMax = ImGui::GetItemRectMax();
    if (mainAsset != nullptr)
    {
        dragAsset(mainAsset->id, mainAsset->type, mainAsset->name);
        const bool doubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        if (mainAsset->type == asset::AssetType::Model && doubleClicked)
        {
            requestInstantiateModel(state, mainAsset->id, core::Uuid{});
        }
        if (mainAsset->type == asset::AssetType::Scene && state.mode == ToolsMode::Editor && doubleClicked)
        {
            if (const std::optional<std::filesystem::path> path = database.project().absolutePath(source.path))
            {
                openSceneTab(state, scene, *path);
            }
        }
    }
    drawSourceMenu(state, scene, source, mainAsset);

    // The import status at the right of the row, when it needs attention.
    if (source.status != asset::ImportStatus::Ready)
    {
        const bool failed = source.status == asset::ImportStatus::Failed;
        const IconText icon = failed ? icons::CircleX : icons::Loader;
        const float width = ImGui::CalcTextSize(icon.c_str()).x;
        const ImVec2 position(rowMax.x - width - ImGui::GetStyle().FramePadding.x,
                              rowMin.y + (rowMax.y - rowMin.y - ImGui::GetFontSize()) * 0.5f);
        ImGui::GetWindowDrawList()->AddText(position, uiColorU32(failed ? colors.error : colors.warning), icon.c_str());
        if (ImGui::IsMouseHoveringRect(position, position + ImVec2(width, ImGui::GetFontSize())))
        {
            ImGui::SetTooltip("%s", failed ? source.error.c_str() : "Importing...");
        }
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
            ImGui::PushID(id.uuid.toString().c_str());
            const EntityIcon icon = info->type == asset::AssetType::Mesh       ? EntityIcon{icons::Box, colors.entity}
                                    : info->type == asset::AssetType::Material ? EntityIcon{icons::Palette, colors.material}
                                    : info->type == asset::AssetType::Texture  ? EntityIcon{icons::Image, colors.texture}
                                                                               : EntityIcon{icons::File, colors.neutral};
            static_cast<void>(treeRow("##asset", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen, icon,
                                      info->name));
            dragAsset(info->id, info->type, info->name);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void drawFolder(ToolsState& state, scene::Scene& scene, const std::string& name, const Folder& folder, int depth)
{
    const ThemeColors& colors = themeColors();
    ImGui::PushID(name.c_str());
    const ImGuiID id = ImGui::GetID("##folder");
    const bool openByDefault = depth <= 1;
    const bool wasOpen = ImGui::GetStateStorage()->GetInt(id, openByDefault ? 1 : 0) != 0;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (openByDefault)
    {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }
    const bool open = treeRow("##folder", flags, {wasOpen ? icons::FolderOpen : icons::Folder, colors.folder}, name);
    if (open)
    {
        for (const auto& [childName, child] : folder.folders)
        {
            drawFolder(state, scene, childName, child, depth + 1);
        }
        for (const asset::SourceFile* const source : folder.files)
        {
            drawSource(state, scene, *source, false);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

} // namespace

void drawAssetsPanel(ToolsState& state, scene::Scene& scene)
{
    if (ImGui::Begin(assetsWindow))
    {
        if (state.database == nullptr)
        {
            ImGui::TextDisabled("No project: set ApplicationConfig::project to import assets.");
            ImGui::End();
            return;
        }

        const asset::AssetDatabase& database = *state.database;
        searchField("##filter", state.assetFilter, "Filter Files");
        ImGui::SetItemTooltip(state.mode == ToolsMode::Editor
                                  ? "Drag a model into the viewport or the scene tree, or an asset onto a property. "
                                    "Double-click a scene to open it."
                                  : "Drag a model into the scene tree, or an asset onto a property.");

        ImGui::PushStyleColor(ImGuiCol_ChildBg, uiColor(themeColors().field));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::GetStyle().FrameRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
        if (ImGui::BeginChild("files", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
            const std::vector<asset::SourceFile> sources = database.sources();
            if (state.assetFilter.empty())
            {
                drawFolder(state, scene, "res://", buildTree(sources), 0);
            }
            else
            {
                // A filter lists the matching files with their whole path.
                for (const asset::SourceFile& source : sources)
                {
                    if (containsIgnoringCase(source.path, state.assetFilter))
                    {
                        drawSource(state, scene, source, true);
                    }
                }
            }
            ImGui::PopStyleVar();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }
    ImGui::End();
}

} // namespace devex::tools::detail
