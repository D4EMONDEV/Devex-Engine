#include "ToolsState.hpp"

#include <devex/asset/Project.hpp>
#include <devex/core/BuildInfo.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/platform/Process.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <format>

namespace devex::tools::detail {
namespace {

constexpr const char* createPopup = "Create New Project";
constexpr const char* renamePopup = "Rename Project";
constexpr const char* removePopup = "Remove Project";

[[nodiscard]] std::int64_t fileTime(const std::filesystem::path& file)
{
    std::error_code error;
    const std::filesystem::file_time_type time = std::filesystem::last_write_time(file, error);
    if (error)
    {
        return 0;
    }
    const auto system = std::chrono::clock_cast<std::chrono::system_clock>(time);
    return std::chrono::duration_cast<std::chrono::seconds>(system.time_since_epoch()).count();
}

[[nodiscard]] std::string formatTime(std::int64_t seconds)
{
    if (seconds <= 0)
    {
        return {};
    }
    const std::chrono::sys_seconds time{std::chrono::seconds(seconds)};
    try
    {
        const std::chrono::zoned_time local(std::chrono::current_zone(), time);
        return std::format("{:%Y-%m-%d %H:%M}", local.get_local_time());
    }
    catch (const std::exception&)
    {
        return std::format("{:%Y-%m-%d %H:%M}", time);
    }
}

void refreshProjects(ToolsState& state)
{
    ProjectManagerState& manager = state.projectManager;
    manager.refresh = false;
    manager.projects.clear();
    for (const ProjectEntry& entry : state.projects.entries())
    {
        ProjectInfo info;
        const core::Result<asset::Project> project = asset::loadProject(entry.file);
        info.exists = project.has_value();
        info.name = project ? project->name : core::toUtf8(entry.file.stem());
        info.modified = fileTime(entry.file);
        manager.projects[core::toUtf8(entry.file)] = std::move(info);
    }
}

[[nodiscard]] const ProjectInfo* infoOf(const ToolsState& state, const std::filesystem::path& file)
{
    const auto found = state.projectManager.projects.find(core::toUtf8(file));
    return found != state.projectManager.projects.end() ? &found->second : nullptr;
}

void openProject(ToolsState& state, const std::filesystem::path& file)
{
    const ProjectInfo* const info = infoOf(state, file);
    if (info == nullptr || !info->exists)
    {
        DEVEX_LOG_ERROR("The project '{}' is missing", core::toUtf8(file));
        return;
    }
    state.requests.openProject = file;
}

void runProject(ToolsState& state, const std::filesystem::path& file)
{
#ifdef _WIN32
    const std::filesystem::path player = state.platform.baseDirectory() / "devex-player.exe";
#else
    const std::filesystem::path player = state.platform.baseDirectory() / "devex-player";
#endif
    const std::array<std::string, 2> arguments{core::toUtf8(player), core::toUtf8(file)};
    if (core::Result<void> launched = platform::Process::launch(arguments, file.parent_path()); !launched)
    {
        DEVEX_LOG_ERROR("Cannot run the project: {}", launched.error());
        return;
    }
    DEVEX_LOG_INFO("Running {} in devex-player", core::toUtf8(file.stem()));
}

void showFolderDialog(ToolsState& state, std::optional<std::filesystem::path> DialogAnswers::*answer)
{
    std::weak_ptr<DialogAnswers> answers = state.dialogAnswers;
    state.platform.showFileDialog(state.window, {.type = platform::FileDialogType::OpenFolder},
                                  [answers, answer](std::optional<std::filesystem::path> chosen) {
                                      if (const std::shared_ptr<DialogAnswers> inbox = answers.lock(); inbox && chosen)
                                      {
                                          (*inbox).*answer = std::move(chosen);
                                      }
                                  });
}

void showImportDialog(ToolsState& state)
{
    std::weak_ptr<DialogAnswers> answers = state.dialogAnswers;
    state.platform.showFileDialog(state.window,
                                  {.type = platform::FileDialogType::OpenFile, .filters = {{"Devex projects", "dvxproj"}}},
                                  [answers](std::optional<std::filesystem::path> chosen) {
                                      if (const std::shared_ptr<DialogAnswers> inbox = answers.lock(); inbox && chosen)
                                      {
                                          inbox->importProject = std::move(chosen);
                                      }
                                  });
}

// Why the new project cannot be created, or nothing.
[[nodiscard]] std::optional<std::string> createProblem(const ProjectManagerState& manager,
                                                       const std::filesystem::path& directory)
{
    if (manager.createName.empty())
    {
        return "The project needs a name.";
    }
    if (manager.createName.find_first_of("<>:\"/\\|?*") != std::string::npos)
    {
        return "The name cannot contain < > : \" / \\ | ? *";
    }
    std::error_code error;
    if (manager.createParent.empty() || !std::filesystem::is_directory(core::pathFromUtf8(manager.createParent), error))
    {
        return "Choose an existing folder for the project.";
    }
    if (std::filesystem::exists(directory, error) && !std::filesystem::is_empty(directory, error))
    {
        return "The project folder must be empty.";
    }
    return std::nullopt;
}

void createProject(ToolsState& state, const std::filesystem::path& directory)
{
    const core::Result<asset::Project> project = asset::createProject(directory, state.projectManager.createName);
    if (!project)
    {
        DEVEX_LOG_ERROR("Cannot create the project: {}", project.error());
        return;
    }
    const std::filesystem::path scenePath = project->assetsDirectory() / "scenes" / "Main.dvxscene";
    if (core::Result<void> saved = scene::saveSceneFile(makeDefaultScene(), scenePath); !saved)
    {
        DEVEX_LOG_WARNING("Cannot write the first scene: {}", saved.error());
    }
    state.projects.add(project->file);
    state.projectManager.refresh = true;
    saveUserSettings(state);
    DEVEX_LOG_INFO("Created project {} in {}", project->name, core::toUtf8(project->root));
    state.requests.openProject = project->file;
}

void drawCreatePopup(ToolsState& state)
{
    ProjectManagerState& manager = state.projectManager;
    const ThemeColors& colors = themeColors();
    const ImGuiStyle& style = ImGui::GetStyle();
    if (std::exchange(manager.openCreate, false))
    {
        ImGui::OpenPopup(createPopup);
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 34.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(createPopup, nullptr, ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }
    const std::filesystem::path parent = core::pathFromUtf8(manager.createParent);
    const std::filesystem::path directory =
        manager.createFolder ? parent / core::pathFromUtf8(manager.createName) : parent;

    ImGui::TextUnformatted("Project Name:");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::InputText("##name", &manager.createName);
    ImGui::Spacing();
    ImGui::TextUnformatted("Project Path:");
    const float browseWidth = ImGui::CalcTextSize("Browse").x + ImGui::CalcTextSize(icons::FolderOpen.c_str()).x +
                              style.FramePadding.x * 2.0f + ImGui::CalcTextSize("  ").x;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browseWidth - style.ItemSpacing.x);
    ImGui::InputText("##parent", &manager.createParent);
    ImGui::SameLine();
    if (labelButton(icons::FolderOpen, "Browse"))
    {
        showFolderDialog(state, &DialogAnswers::newProjectLocation);
    }
    ImGui::Checkbox("Create folder", &manager.createFolder);

    ImGui::Spacing();
    const std::optional<std::string> problem = createProblem(manager, directory);
    if (problem)
    {
        iconLabel(icons::CircleX, colors.error);
        ImGui::TextColored(uiColor(colors.error), "%s", problem->c_str());
    }
    else
    {
        iconLabel(icons::CircleCheck, colors.success);
        ImGui::TextColored(uiColor(colors.success), "The project goes in %s", core::toUtf8(directory).c_str());
    }

    ImGui::Dummy(ImVec2(0.0f, style.ItemSpacing.y));
    const float buttonWidth = ImGui::GetFontSize() * 8.0f;
    alignRight(buttonWidth * 2.0f + style.ItemSpacing.x);
    if (primaryButton(icons::Plus, "Create & Edit", buttonWidth, !problem.has_value()) ||
        (!problem && ImGui::IsKeyPressed(ImGuiKey_Enter)))
    {
        ImGui::CloseCurrentPopup();
        createProject(state, directory);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void drawRenamePopup(ToolsState& state)
{
    ProjectManagerState& manager = state.projectManager;
    if (std::exchange(manager.openRename, false))
    {
        ImGui::OpenPopup(renamePopup);
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 26.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(renamePopup, nullptr, ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }
    ImGui::TextUnformatted("Project Name:");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::InputText("##name", &manager.renameBuffer);
    ImGui::TextDisabled("The folder and the project file keep their names.");
    ImGui::Spacing();
    const float buttonWidth = ImGui::GetFontSize() * 7.0f;
    alignRight(buttonWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x);
    const bool valid = !manager.renameBuffer.empty();
    if (primaryButton(icons::Check, "Rename", buttonWidth, valid) || (valid && ImGui::IsKeyPressed(ImGuiKey_Enter)))
    {
        ImGui::CloseCurrentPopup();
        core::Result<asset::Project> project = asset::loadProject(manager.selected);
        if (project)
        {
            project->name = manager.renameBuffer;
            if (core::Result<void> saved = asset::saveProject(*project); !saved)
            {
                DEVEX_LOG_ERROR("Cannot rename the project: {}", saved.error());
            }
        }
        else
        {
            DEVEX_LOG_ERROR("Cannot rename the project: {}", project.error());
        }
        manager.refresh = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void drawRemovePopup(ToolsState& state)
{
    ProjectManagerState& manager = state.projectManager;
    const ThemeColors& colors = themeColors();
    if (std::exchange(manager.openRemove, false))
    {
        ImGui::OpenPopup(removePopup);
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(removePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        return;
    }
    const ProjectInfo* const info = infoOf(state, manager.selected);
    iconLabel(icons::TriangleAlert, colors.warning);
    ImGui::Text("Remove %s from the list?", info != nullptr ? info->name.c_str() : "the project");
    ImGui::TextDisabled("The project folder and its files are not modified.");
    ImGui::Spacing();
    const float buttonWidth = ImGui::GetFontSize() * 7.0f;
    alignRight(buttonWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x);
    if (primaryButton(icons::Trash, "Remove", buttonWidth))
    {
        ImGui::CloseCurrentPopup();
        state.projects.remove(manager.selected);
        manager.selected.clear();
        manager.refresh = true;
        saveUserSettings(state);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// One row of the list: favorite star, icon, name and path, and the date it was last edited.
void drawProjectRow(ToolsState& state, const ProjectEntry& entry, bool& open)
{
    ProjectManagerState& manager = state.projectManager;
    const ThemeColors& colors = themeColors();
    const ImGuiStyle& style = ImGui::GetStyle();
    const ProjectInfo* const info = infoOf(state, entry.file);
    const bool exists = info != nullptr && info->exists;
    const std::string path = core::toUtf8(entry.file.parent_path());
    const float lineHeight = ImGui::GetTextLineHeight();
    const float rowHeight = lineHeight * 2.0f + style.FramePadding.y * 4.0f;
    const float iconSize = rowHeight - style.FramePadding.y * 2.0f;

    ImGui::PushID(core::toUtf8(entry.file).c_str());
    const ImVec2 rowStart = ImGui::GetCursorScreenPos();
    const bool selected = manager.selected == entry.file;
    if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap,
                         ImVec2(0.0f, rowHeight)))
    {
        manager.selected = entry.file;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            open = true;
        }
    }
    const float rowWidth = ImGui::GetItemRectSize().x;
    if (ImGui::BeginPopupContextItem("project menu"))
    {
        manager.selected = entry.file;
        if (ImGui::MenuItemEx("Edit", icons::Pencil.c_str(), nullptr, false, exists))
        {
            open = true;
        }
        if (ImGui::MenuItemEx("Run", icons::Play.c_str(), nullptr, false, exists))
        {
            runProject(state, entry.file);
        }
        if (ImGui::MenuItemEx("Show in File Manager", icons::FolderOpen.c_str(), nullptr, false, exists))
        {
            static_cast<void>(state.platform.openPath(entry.file.parent_path()));
        }
        ImGui::Separator();
        if (ImGui::MenuItemEx(entry.favorite ? "Remove from Favorites" : "Add to Favorites", icons::Star.c_str()))
        {
            state.projects.setFavorite(entry.file, !entry.favorite);
            saveUserSettings(state);
        }
        if (ImGui::MenuItemEx("Remove from List", icons::Trash.c_str()))
        {
            manager.openRemove = true;
        }
        ImGui::EndPopup();
    }

    // The star toggles the favorite state without selecting the row.
    const float starWidth = ImGui::GetFrameHeight();
    ImGui::SetCursorScreenPos(ImVec2(rowStart.x + style.FramePadding.x, rowStart.y + (rowHeight - starWidth) * 0.5f));
    if (toolButton("favorite", icons::Star, entry.favorite ? "Remove from favorites" : "Add to favorites", false, true,
                   entry.favorite ? colors.favorite : colors.textDim))
    {
        state.projects.setFavorite(entry.file, !entry.favorite);
        saveUserSettings(state);
    }

    ImDrawList* const draw = ImGui::GetWindowDrawList();
    const float iconX = rowStart.x + style.FramePadding.x * 2.0f + starWidth;
    ImFont* const font = ImGui::GetFont();
    draw->AddText(font, iconSize / 0.9f, ImVec2(iconX, rowStart.y + (rowHeight - iconSize / 0.9f) * 0.5f),
                  exists ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 90), icons::Logo.c_str());

    const float textX = iconX + iconSize + style.ItemSpacing.x * 2.0f;
    const float nameY = rowStart.y + style.FramePadding.y * 1.5f;
    const std::string name = info != nullptr ? info->name : core::toUtf8(entry.file.stem());
    draw->AddText(editorFonts().bold, ImGui::GetFontSize(), ImVec2(textX, nameY),
                  exists ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled), name.c_str());
    const float pathY = nameY + lineHeight + style.FramePadding.y;
    const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    if (exists)
    {
        draw->AddText(ImVec2(textX, pathY), dim, icons::Folder.c_str());
        draw->AddText(ImVec2(textX + ImGui::CalcTextSize(icons::Folder.c_str()).x + style.ItemInnerSpacing.x * 2.0f, pathY),
                      dim, path.c_str());
    }
    else
    {
        draw->AddText(ImVec2(textX, pathY), uiColorU32(colors.error), icons::TriangleAlert.c_str());
        const std::string missing = "Missing: " + path;
        draw->AddText(ImVec2(textX + ImGui::CalcTextSize(icons::TriangleAlert.c_str()).x + style.ItemInnerSpacing.x * 2.0f, pathY),
                      uiColorU32(colors.error), missing.c_str());
    }
    const std::string date = formatTime(std::max(entry.lastOpened, info != nullptr ? info->modified : 0));
    const float dateWidth = ImGui::CalcTextSize(date.c_str()).x;
    draw->AddText(ImVec2(rowStart.x + rowWidth - dateWidth - style.FramePadding.x * 3.0f, pathY), dim, date.c_str());

    ImGui::SetCursorScreenPos(ImVec2(rowStart.x, rowStart.y + rowHeight + style.ItemSpacing.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::PopID();
}

} // namespace

void drawProjectManager(ToolsState& state)
{
    ProjectManagerState& manager = state.projectManager;
    const ThemeColors& colors = themeColors();
    const ImGuiStyle& style = ImGui::GetStyle();
    if (manager.refresh)
    {
        refreshProjects(state);
    }

    const ImGuiViewport* const viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.WindowPadding.x * 1.5f, style.WindowPadding.y * 1.2f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus;
    const bool visible = ImGui::Begin("Project Manager", nullptr, flags);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (!visible)
    {
        ImGui::End();
        return;
    }

    // Title bar: the logo and name, the section, and the settings.
    ImGui::PushFont(editorFonts().bold, style.FontSizeBase * 1.25f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(icons::Logo.c_str());
    ImGui::SameLine();
    ImGui::TextUnformatted("DEVEX");
    ImGui::PopFont();
    const char* const section = "Projects";
    const float sectionWidth = ImGui::CalcTextSize(icons::ListTree.c_str()).x + ImGui::CalcTextSize(section).x +
                               style.ItemInnerSpacing.x;
    ImGui::SameLine((ImGui::GetWindowWidth() - sectionWidth) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, uiColor(colors.accent));
    ImGui::AlignTextToFramePadding();
    iconLabel(icons::ListTree, colors.accent);
    boldText(section);
    ImGui::PopStyleColor();
    const float settingsWidth = ImGui::CalcTextSize("Settings").x + ImGui::CalcTextSize(icons::Settings.c_str()).x +
                                ImGui::CalcTextSize("  ").x + style.FramePadding.x * 2.0f;
    ImGui::SameLine();
    alignRight(settingsWidth);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    if (labelButton(icons::Settings, "Settings"))
    {
        state.showSettings = true;
    }
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Toolbar: create, import, scan, filter and sort.
    const float sideWidth = ImGui::GetFontSize() * 11.0f;
    if (labelButton(icons::Plus, "Create"))
    {
        manager.openCreate = true;
        if (manager.createParent.empty() && !state.projects.entries().empty())
        {
            const auto newest = std::ranges::max_element(state.projects.entries(), {}, &ProjectEntry::lastOpened);
            manager.createParent = core::toUtf8(newest->file.parent_path().parent_path());
        }
    }
    ImGui::SameLine();
    if (labelButton(icons::FolderOpen, "Import"))
    {
        showImportDialog(state);
    }
    ImGui::SameLine();
    if (labelButton(icons::FolderSearch, "Scan"))
    {
        showFolderDialog(state, &DialogAnswers::scanFolder);
    }
    ImGui::SetItemTooltip("Adds the projects found in a folder and its subfolders");
    ImGui::SameLine();
    const char* const sortLabel = "Sort:";
    const float sortComboWidth = ImGui::GetFontSize() * 9.0f;
    const float filterWidth = ImGui::GetContentRegionAvail().x - sideWidth - ImGui::CalcTextSize(sortLabel).x -
                              sortComboWidth - style.ItemSpacing.x * 4.0f;
    searchField("##filter", manager.filter, "Filter Projects", std::max(filterWidth, ImGui::GetFontSize() * 6.0f));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(sortLabel);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(sortComboWidth);
    constexpr std::array<const char*, 3> sortNames{"Last Edited", "Name", "Path"};
    if (beginCombo("##sort", sortNames[static_cast<std::size_t>(manager.sort)]))
    {
        for (std::size_t index = 0; index < sortNames.size(); ++index)
        {
            if (ImGui::Selectable(sortNames[index], static_cast<std::size_t>(manager.sort) == index))
            {
                manager.sort = static_cast<ProjectSort>(index);
            }
        }
        ImGui::EndCombo();
    }

    // The list of projects.
    const float footerHeight = ImGui::GetTextLineHeightWithSpacing() + style.ItemSpacing.y;
    const float listWidth = ImGui::GetContentRegionAvail().x - sideWidth - style.ItemSpacing.x;
    const float listHeight = ImGui::GetContentRegionAvail().y - footerHeight;
    std::optional<std::filesystem::path> opened;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, uiColor(colors.field));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, style.FrameRounding);
    if (ImGui::BeginChild("projects", ImVec2(listWidth, listHeight), ImGuiChildFlags_None))
    {
        const std::vector<ProjectEntry> projects =
            state.projects.sorted(manager.sort, manager.filter, [&state](const ProjectEntry& entry) {
                const ProjectInfo* const info = infoOf(state, entry.file);
                return info != nullptr ? info->name : core::toUtf8(entry.file.stem());
            });
        if (projects.empty())
        {
            const char* const hint = state.projects.entries().empty()
                                         ? "No project yet: create one, or import or scan existing ones."
                                         : "No project matches the filter.";
            const ImVec2 size = ImGui::CalcTextSize(hint);
            ImGui::SetCursorPos(ImVec2((ImGui::GetWindowWidth() - size.x) * 0.5f, (ImGui::GetWindowHeight() - size.y) * 0.4f));
            ImGui::TextDisabled("%s", hint);
        }
        // The first project is selected until the user picks another, as the most likely to open.
        if (!projects.empty() && std::ranges::none_of(projects, [&](const ProjectEntry& entry) {
                return entry.file == manager.selected;
            }))
        {
            manager.selected = projects.front().file;
        }
        for (const ProjectEntry& entry : projects)
        {
            bool open = false;
            drawProjectRow(state, entry, open);
            if (open)
            {
                opened = entry.file;
            }
        }
        if (!manager.selected.empty() && ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter))
        {
            opened = manager.selected;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // The actions on the selected project.
    ImGui::SameLine();
    ImGui::BeginGroup();
    const ProjectInfo* const selected = manager.selected.empty() ? nullptr : infoOf(state, manager.selected);
    const bool canEdit = selected != nullptr && selected->exists;
    if (labelButton(icons::Pencil, "Edit", sideWidth, canEdit))
    {
        opened = manager.selected;
    }
    if (labelButton(icons::Play, "Run", sideWidth, canEdit))
    {
        runProject(state, manager.selected);
    }
    ImGui::SetItemTooltip("Runs the startup scene in devex-player");
    if (labelButton(icons::TextCursor, "Rename", sideWidth, canEdit))
    {
        manager.renameBuffer = selected->name;
        manager.openRename = true;
    }
    if (labelButton(icons::FolderOpen, "Show in Folder", sideWidth, canEdit))
    {
        static_cast<void>(state.platform.openPath(manager.selected.parent_path()));
    }
    ImGui::Spacing();
    if (labelButton(icons::Trash, "Remove", sideWidth, selected != nullptr))
    {
        manager.openRemove = true;
    }
    if (labelButton(icons::ListX, "Remove Missing", sideWidth))
    {
        if (const std::size_t removed = state.projects.removeMissing(); removed > 0)
        {
            DEVEX_LOG_INFO("Removed {} missing project{}", removed, removed == 1 ? "" : "s");
            manager.refresh = true;
            saveUserSettings(state);
        }
    }
    ImGui::EndGroup();

    const std::string version = std::format("v{} {}", core::version(), core::buildType());
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ImGui::CalcTextSize(version.c_str()).x - style.WindowPadding.x);
    ImGui::TextDisabled("%s", version.c_str());

    drawCreatePopup(state);
    drawRenamePopup(state);
    drawRemovePopup(state);
    ImGui::End();

    if (opened)
    {
        openProject(state, *opened);
    }
}

} // namespace devex::tools::detail
