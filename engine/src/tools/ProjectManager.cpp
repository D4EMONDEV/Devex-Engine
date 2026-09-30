// The project manager, the first screen of the editor, made with the interface of the engine: a
// canvas of entities in a panel of its own, drawn into an image that its ImGui window shows.
#include "EditorUi.hpp"
#include "ToolsState.hpp"

#include <devex/core/Profiler.hpp>
#include <devex/asset/Project.hpp>
#include <devex/core/BuildInfo.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/platform/Process.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/ui/TextLayout.hpp>

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <format>

namespace devex::tools::detail {

using scene::Entity;
using scene::UiRect;
using namespace rects;
using Button = PanelButton;

namespace {

// The image the panel is drawn into, among the interface surfaces of the editor.
constexpr std::uint32_t projectManagerSurface = 1;

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
        if (project && state.projectCodeStatus)
        {
            info.code = state.projectCodeStatus(*project);
        }
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

// One project of the list and what its row shows.
struct Row
{
    std::filesystem::path file;
    Entity row;
    Entity star;
    Entity starIcon;
    Entity logo;
    Entity name;
    Entity update;
    Entity pathIcon;
    Entity path;
    Entity date;
};

} // namespace

// The panel and the entities the code reads and changes, beside the ones it only places.
struct ProjectManagerUi : PanelBuilder
{
    ProjectManagerUi()
        : PanelBuilder(projectManagerSurface)
    {
    }

    float sideWidth = 175.0f;
    bool built = false;

    Entity window;
    Button settings;
    Button create;
    Button import;
    Button scan;
    Entity filter;
    Entity sort;
    Entity list;
    Entity scroll;
    Entity rowsColumn;
    Entity emptyHint;
    std::vector<Row> rows;
    Button edit;
    Button run;
    Button rename;
    Button show;
    Button remove;
    Button removeMissing;
    Entity updateTitle;
    Entity updateMessage;
    Entity updateHint;
    Entity version;

    Entity rowMenu;
    Button menuEdit;
    Button menuRun;
    Button menuShow;
    Button menuFavorite;
    Button menuRemove;

    Entity createDialog;
    Entity createName;
    Entity createParent;
    Button browse;
    Entity createFolder;
    Entity createStatusIcon;
    Entity createStatus;
    Button createConfirm;
    Button createCancel;
    std::string syncedParent;

    Entity renameDialog;
    Entity renameName;
    Button renameConfirm;
    Button renameCancel;

    Entity removeDialog;
    Entity removeText;
    Button removeConfirm;
    Button removeCancel;

    void build(ToolsState& state, EditorUiKit& kit);
    void buildRows(ToolsState& state, EditorUiKit& kit, const std::vector<ProjectEntry>& projects);
    void update(ToolsState& state, EditorUiKit& kit, core::Duration delta);
};

namespace {

[[nodiscard]] std::string projectName(const ToolsState& state, const ProjectEntry& entry)
{
    const ProjectInfo* const info = infoOf(state, entry.file);
    return info != nullptr ? info->name : core::toUtf8(entry.file.stem());
}

} // namespace

void ProjectManagerUi::build(ToolsState& state, EditorUiKit& kit)
{
    built = true;
    const float line = font * 2.0f;
    sideWidth = font * 12.5f;

    window = add({}, "Window", whole(), "window");
    scene().add<scene::UiImage>(window);
    scene().add<scene::UiLayout>(window, scene::UiLayout{.kind = scene::UiLayoutKind::Column,
                                                         .spacing = font * 0.7f,
                                                         .padding = {font * 1.2f, font * 0.9f, font * 1.2f, font * 0.7f},
                                                         .align = scene::TextAlign::Left});

    // Title bar: the logo and name on the left, the section in the middle, the settings on the right.
    const Entity title = add(window, "Title", wide(font * 2.4f));
    const Entity brand = add(title, "Brand",
                             UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {font * 12.0f, 0.0f}});
    scene().add<scene::UiLayout>(brand, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                        .spacing = font * 0.5f,
                                                        .align = scene::TextAlign::Left});
    const float logo = font * 1.7f;
    const Entity logoImage = add(brand, "Logo", middle({logo, logo}));
    scene().add<scene::UiImage>(logoImage, scene::UiImage{.texture = kit.icon(Icon::Logo), .raycastTarget = false});
    text(brand, middle({font * 6.0f, font * 2.0f}), "DEVEX", "text", true, scene::TextAlign::Left, font * 1.3f);
    const float sectionWidth = font * 1.2f + font * 0.5f + kit.textWidth(EditorUiKit::boldFont(), "Projects", font);
    const Entity section = add(title, "Section",
                               UiRect{.anchorMin = {0.5f, 0.0f},
                                      .anchorMax = {0.5f, 1.0f},
                                      .offsetMin = {-sectionWidth * 0.5f, 0.0f},
                                      .offsetMax = {sectionWidth * 0.5f, 0.0f}});
    scene().add<scene::UiLayout>(section, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                          .spacing = font * 0.5f,
                                                          .align = scene::TextAlign::Center});
    icon(kit, section, middle({font * 1.2f, font * 1.2f}), Icon::ListTree, "icon_accent");
    text(section, middle({kit.textWidth(EditorUiKit::boldFont(), "Projects", font) + 2.0f, line}), "Projects", "accent", true);
    const Entity right = add(title, "Right",
                             UiRect{.anchorMin = {1.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {-font * 12.0f, 0.0f}, .offsetMax = {0.0f, 0.0f}});
    scene().add<scene::UiLayout>(right, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .align = scene::TextAlign::Right});
    settings = button(kit, right, Icon::Settings, "Settings", "flat");

    // Toolbar: create, import, scan, then the filter and the sort.
    const Entity toolbar = add(window, "Toolbar", wide(line));
    scene().add<scene::UiLayout>(toolbar, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                          .spacing = font * 0.45f,
                                                          .align = scene::TextAlign::Left});
    create = button(kit, toolbar, Icon::Plus, "Create");
    import = button(kit, toolbar, Icon::FolderOpen, "Import");
    scan = button(kit, toolbar, Icon::FolderSearch, "Scan");
    tooltip(scan.entity, "Adds the projects found in a folder and its subfolders");
    add(toolbar, "Gap", middle({font * 0.4f, 1.0f}));
    filter = searchField(kit, toolbar, grow(line), state.projectManager.filter, "Filter Projects");
    text(toolbar, middle({kit.textWidth(EditorUiKit::regularFont(), "Sort:", font) + font * 0.6f, line}), "Sort:", "text",
         false, scene::TextAlign::Right);
    sort = add(toolbar, "Sort", middle({font * 10.0f, line}), "dropdown");
    scene().add<scene::UiImage>(sort);
    scene().add<scene::UiButton>(sort);
    scene().add<scene::UiText>(sort, scene::UiText{.font = EditorUiKit::regularFont(),
                                                   .size = font,
                                                   .align = scene::TextAlign::Left,
                                                   .verticalAlign = scene::TextVerticalAlign::Middle,
                                                   .wrap = false});
    scene().add<scene::UiDropdown>(sort, scene::UiDropdown{.options = {"Last Edited", "Name", "Path"},
                                                           .selected = static_cast<std::int32_t>(state.projectManager.sort),
                                                           .action = "sort"});

    // The list of projects, and the actions on the selected one.
    const Entity body = add(window, "Body", whole());
    scene().add<scene::UiLayout>(body, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                       .spacing = font * 0.7f,
                                                       .align = scene::TextAlign::Left});
    list = add(body, "List", whole(), "list");
    scene().add<scene::UiImage>(list, scene::UiImage{.raycastTarget = false});
    scroll = add(list, "Scroll", whole(math::Vec4{2.0f}), "scroll");
    scene().add<scene::UiScroll>(scroll, scene::UiScroll{.speed = font * 4.0f});
    rowsColumn = add(scroll, "Rows",
                     UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {4.0f, 4.0f}, .offsetMax = {-12.0f, 4.0f}});
    scene().add<scene::UiLayout>(rowsColumn, scene::UiLayout{.kind = scene::UiLayoutKind::Column,
                                                             .spacing = font * 0.3f,
                                                             .align = scene::TextAlign::Left});
    emptyHint = text(list, whole(), "", "dim", false, scene::TextAlign::Center);

    const Entity side = add(body, "Actions", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {sideWidth, 0.0f}});
    scene().add<scene::UiLayout>(side, scene::UiLayout{.kind = scene::UiLayoutKind::Column,
                                                       .spacing = font * 0.45f,
                                                       .align = scene::TextAlign::Left});
    edit = button(kit, side, Icon::Pencil, "Edit", "button", -1.0f, 0.0f, scene::TextAlign::Left);
    run = button(kit, side, Icon::Play, "Run", "button", -1.0f, 0.0f, scene::TextAlign::Left);
    rename = button(kit, side, Icon::TextCursor, "Rename", "button", -1.0f, 0.0f, scene::TextAlign::Left);
    show = button(kit, side, Icon::FolderOpen, "Show in Folder", "button", -1.0f, 0.0f, scene::TextAlign::Left);
    add(side, "Gap", wide(font * 0.5f));
    remove = button(kit, side, Icon::Trash, "Remove", "button", -1.0f, 0.0f, scene::TextAlign::Left);
    removeMissing = button(kit, side, Icon::ListX, "Remove Missing", "button", -1.0f, 0.0f, scene::TextAlign::Left);
    add(side, "Gap", wide(font * 0.5f));
    updateTitle = text(side, wide(font * 1.5f), "Code update required", "warning");
    updateMessage = text(side, wide(font * 4.5f), "", "text");
    updateHint = text(side, wide(font * 3.0f), "Opening this project will rebuild its game code.", "dim");
    for (const Entity wrapped : {updateMessage, updateHint})
    {
        scene::UiText& shown = scene().get<scene::UiText>(wrapped);
        shown.wrap = true;
        shown.verticalAlign = scene::TextVerticalAlign::Top;
    }

    const std::string versionText = std::format("v{} {}", core::version(), core::buildType());
    version = text(window, wide(font * 1.3f), versionText, "dim", false, scene::TextAlign::Right);

    // The menu of a row, opened with the second button.
    rowMenu = add({}, "Row menu", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {font * 15.0f, font * 11.4f}, .visible = false}, "popup");
    scene().add<scene::UiImage>(rowMenu);
    scene().add<scene::UiPopup>(rowMenu);
    scene().add<scene::UiLayout>(rowMenu, scene::UiLayout{.kind = scene::UiLayoutKind::Column,
                                                          .spacing = 1.0f,
                                                          .padding = math::Vec4{font * 0.3f},
                                                          .align = scene::TextAlign::Left});
    const float item = font * 1.9f;
    menuEdit = button(kit, rowMenu, Icon::Pencil, "Edit", "menu_item", -1.0f, item, scene::TextAlign::Left);
    menuRun = button(kit, rowMenu, Icon::Play, "Run", "menu_item", -1.0f, item, scene::TextAlign::Left);
    menuShow = button(kit, rowMenu, Icon::FolderOpen, "Show in File Manager", "menu_item", -1.0f, item, scene::TextAlign::Left);
    const Entity separator = add(rowMenu, "Separator", wide(1.0f), "separator");
    scene().add<scene::UiImage>(separator, scene::UiImage{.raycastTarget = false});
    menuFavorite = button(kit, rowMenu, Icon::Star, "Add to Favorites", "menu_item", -1.0f, item, scene::TextAlign::Left);
    menuRemove = button(kit, rowMenu, Icon::Trash, "Remove from List", "menu_item", -1.0f, item, scene::TextAlign::Left);

    // Creating a project: its name, the folder it goes in, and whether a folder of its name is made.
    createDialog = dialog("Create dialog", {font * 34.0f, font * 19.5f});
    text(createDialog, wide(font * 1.6f), "Create New Project", "text", true, scene::TextAlign::Left, font * 1.15f);
    text(createDialog, wide(font * 1.4f), "Project Name:", "text");
    createName = field(createDialog, wide(line), state.projectManager.createName, "", "create");
    text(createDialog, wide(font * 1.4f), "Project Path:", "text");
    const Entity pathLine = add(createDialog, "Path", wide(line));
    scene().add<scene::UiLayout>(pathLine, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                           .spacing = font * 0.45f,
                                                           .align = scene::TextAlign::Left});
    createParent = field(pathLine, grow(line), state.projectManager.createParent, "", "create");
    browse = button(kit, pathLine, Icon::FolderOpen, "Browse");
    const Entity folderLine = add(createDialog, "Folder", wide(font * 1.6f));
    scene().add<scene::UiLayout>(folderLine, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                             .spacing = font * 0.5f,
                                                             .align = scene::TextAlign::Left});
    createFolder = add(folderLine, "Create folder", middle({font * 1.2f, font * 1.2f}), "toggle");
    scene().add<scene::UiImage>(createFolder);
    scene().add<scene::UiToggle>(createFolder, scene::UiToggle{.value = state.projectManager.createFolder});
    text(folderLine, middle({font * 8.0f, font * 1.6f}), "Create folder", "text");
    const Entity statusLine = add(createDialog, "Status", wide(font * 1.6f));
    scene().add<scene::UiLayout>(statusLine, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                             .spacing = font * 0.5f,
                                                             .align = scene::TextAlign::Left});
    createStatusIcon = icon(kit, statusLine, middle({font * 1.1f, font * 1.1f}), Icon::CircleCheck, "icon_success");
    createStatus = text(statusLine, grow(font * 1.6f), "", "success");
    add(createDialog, "Gap", wide(font * 0.3f));
    std::tie(createConfirm, createCancel) = dialogButtons(kit, createDialog, Icon::Plus, "Create & Edit", font * 9.0f);

    renameDialog = dialog("Rename dialog", {font * 26.0f, font * 12.5f});
    text(renameDialog, wide(font * 1.6f), "Rename Project", "text", true, scene::TextAlign::Left, font * 1.15f);
    text(renameDialog, wide(font * 1.4f), "Project Name:", "text");
    renameName = field(renameDialog, wide(line), "", "", "rename");
    text(renameDialog, wide(font * 1.4f), "The folder and the project file keep their names.", "dim");
    std::tie(renameConfirm, renameCancel) = dialogButtons(kit, renameDialog, Icon::Check, "Rename", font * 7.0f);

    removeDialog = dialog("Remove dialog", {font * 28.0f, font * 10.0f});
    const Entity warning = add(removeDialog, "Question", wide(font * 1.8f));
    scene().add<scene::UiLayout>(warning, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                          .spacing = font * 0.5f,
                                                          .align = scene::TextAlign::Left});
    icon(kit, warning, middle({font * 1.2f, font * 1.2f}), Icon::TriangleAlert, "icon_warning");
    removeText = text(warning, grow(font * 1.8f), "", "text");
    text(removeDialog, wide(font * 1.4f), "The project folder and its files are not modified.", "dim");
    std::tie(removeConfirm, removeCancel) = dialogButtons(kit, removeDialog, Icon::Trash, "Remove", font * 7.0f);
}

void ProjectManagerUi::buildRows(ToolsState& state, EditorUiKit& kit, const std::vector<ProjectEntry>& projects)
{
    for (const Row& row : rows)
    {
        scene().destroyEntity(row.row);
    }
    rows.clear();
    const float height = font * 3.5f;
    for (const ProjectEntry& entry : projects)
    {
        Row row{.file = entry.file};
        row.row = add(rowsColumn, "Row", wide(height), "row");
        scene().add<scene::UiImage>(row.row);
        scene().add<scene::UiButton>(row.row);
        scene().add<scene::UiContextMenu>(row.row, scene::UiContextMenu{.popup = scene().reference(rowMenu)});

        // The star toggles the favorite without selecting the row.
        const float star = font * 1.8f;
        row.star = add(row.row, "Star",
                       UiRect{.anchorMin = {0.0f, 0.5f},
                              .anchorMax = {0.0f, 0.5f},
                              .offsetMin = {font * 0.4f, -star * 0.5f},
                              .offsetMax = {font * 0.4f + star, star * 0.5f}},
                       "flat");
        scene().add<scene::UiImage>(row.star);
        scene().add<scene::UiButton>(row.star);
        row.starIcon = icon(kit, row.star, whole(math::Vec4{font * 0.35f}), Icon::Star, "icon_dim");

        const float logo = font * 2.4f;
        const float logoLeft = font * 0.4f + star + font * 0.5f;
        row.logo = add(row.row, "Logo",
                       UiRect{.anchorMin = {0.0f, 0.5f},
                              .anchorMax = {0.0f, 0.5f},
                              .offsetMin = {logoLeft, -logo * 0.5f},
                              .offsetMax = {logoLeft + logo, logo * 0.5f}});
        scene().add<scene::UiImage>(row.logo, scene::UiImage{.texture = kit.icon(Icon::Logo), .raycastTarget = false});

        const float left = logoLeft + logo + font * 0.9f;
        const float dateWidth = font * 9.0f;
        const float top = (height - font * 2.8f) * 0.5f;
        row.name = text(row.row,
                        UiRect{.anchorMin = {0.0f, 0.0f},
                               .anchorMax = {1.0f, 0.0f},
                               .offsetMin = {left, top},
                               .offsetMax = {-dateWidth, top + font * 1.4f}},
                        projectName(state, entry), "text", true);
        const float updateWidth = kit.textWidth(EditorUiKit::regularFont(), "Code update required", font) + 4.0f;
        row.update = text(row.row,
                          UiRect{.anchorMin = {1.0f, 0.0f},
                                 .anchorMax = {1.0f, 0.0f},
                                 .offsetMin = {-updateWidth - font * 0.8f, top},
                                 .offsetMax = {-font * 0.8f, top + font * 1.4f}},
                          "Code update required", "warning", false, scene::TextAlign::Right);
        scene().get<scene::UiText>(row.update).raycastTarget = true;
        row.pathIcon = icon(kit, row.row,
                            UiRect{.anchorMin = {0.0f, 0.0f},
                                   .anchorMax = {0.0f, 0.0f},
                                   .offsetMin = {left, top + font * 1.55f},
                                   .offsetMax = {left + font, top + font * 2.55f}},
                            Icon::Folder, "icon_dim");
        row.path = text(row.row,
                        UiRect{.anchorMin = {0.0f, 0.0f},
                               .anchorMax = {1.0f, 0.0f},
                               .offsetMin = {left + font * 1.4f, top + font * 1.4f},
                               .offsetMax = {-dateWidth, top + font * 2.8f}},
                        core::toUtf8(entry.file.parent_path()), "dim");
        row.date = text(row.row,
                        UiRect{.anchorMin = {1.0f, 0.0f},
                               .anchorMax = {1.0f, 0.0f},
                               .offsetMin = {-dateWidth, top + font * 1.4f},
                               .offsetMax = {-font * 0.8f, top + font * 2.8f}},
                        "", "dim", false, scene::TextAlign::Right);
        rows.push_back(std::move(row));
    }
    // The column is as tall as its rows, for the list to scroll through them.
    UiRect& column = scene().get<UiRect>(rowsColumn);
    const auto count = static_cast<float>(rows.size());
    column.offsetMax.y = column.offsetMin.y + count * height + std::max(count - 1.0f, 0.0f) * font * 0.3f;
}

void ProjectManagerUi::update(ToolsState& state, EditorUiKit& kit, core::Duration delta)
{
    ProjectManagerState& manager = state.projectManager;
    if (manager.refresh)
    {
        refreshProjects(state);
    }
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    font = state.theme.fontSize;
    if (!built)
    {
        build(state, kit);
    }
    ui::UiWorld& world = panel.world();
    styleTooltips(colors);

    // The rows follow the list, its sort and its filter.
    manager.filter = scene().get<scene::UiText>(filter).text;
    const std::vector<ProjectEntry> projects =
        state.projects.sorted(manager.sort, manager.filter, [&state](const ProjectEntry& entry) { return projectName(state, entry); });
    const bool same = projects.size() == rows.size() &&
                      std::ranges::equal(projects, rows, {}, &ProjectEntry::file, &Row::file);
    if (!same)
    {
        buildRows(state, kit, projects);
    }
    // The first project is selected until the user picks another, as the most likely to open.
    if (!projects.empty() && std::ranges::none_of(projects, [&](const ProjectEntry& entry) { return entry.file == manager.selected; }))
    {
        manager.selected = projects.front().file;
    }
    scene::UiText& hint = scene().get<scene::UiText>(emptyHint);
    hint.text = !projects.empty()                ? std::string{}
                : state.projects.entries().empty() ? "No project yet: create one, or import or scan existing ones."
                                                   : "No project matches the filter.";

    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        const Row& row = rows[index];
        const ProjectEntry& entry = projects[index];
        const ProjectInfo* const info = infoOf(state, entry.file);
        const bool exists = info != nullptr && info->exists;
        const bool needsUpdate = exists && info->code.needsUpdate;
        scene().get<UiRect>(row.row).style = entry.file == manager.selected ? "row_selected" : "row";
        scene().get<UiRect>(row.starIcon).style = entry.favorite ? "icon_favorite" : "icon_dim";
        tooltip(row.star, entry.favorite ? "Remove from favorites" : "Add to favorites");
        scene().get<scene::UiText>(row.name).text = projectName(state, entry);
        scene().get<UiRect>(row.name).style = exists ? "text" : "dim";
        scene().get<UiRect>(row.logo).opacity = exists ? 1.0f : 0.35f;
        scene().get<UiRect>(row.update).visible = needsUpdate;
        if (needsUpdate)
        {
            tooltip(row.update, info->code.message + "\nOpening this project will rebuild its game code.");
        }
        const std::string folder = core::toUtf8(entry.file.parent_path());
        scene().get<scene::UiText>(row.path).text = exists ? folder : "Missing: " + folder;
        scene().get<UiRect>(row.path).style = exists ? "dim" : "error";
        scene().get<scene::UiImage>(row.pathIcon).texture = kit.icon(exists ? Icon::Folder : Icon::TriangleAlert);
        scene().get<UiRect>(row.pathIcon).style = exists ? "icon_dim" : "icon_error";
        scene().get<scene::UiText>(row.date).text = formatTime(std::max(entry.lastOpened, info != nullptr ? info->modified : 0));
    }

    // The actions on the selected project.
    const ProjectInfo* const selected = manager.selected.empty() ? nullptr : infoOf(state, manager.selected);
    const bool canEdit = selected != nullptr && selected->exists;
    const bool needsUpdate = canEdit && selected->code.needsUpdate;
    relabel(kit, edit, needsUpdate ? "Update & Edit" : "Edit");
    enable(edit, canEdit);
    enable(run, canEdit && !needsUpdate);
    tooltip(run.entity, needsUpdate ? "Open with Update & Edit to rebuild the game code before running."
                                    : "Runs the startup scene in devex-player");
    enable(rename, canEdit);
    enable(show, canEdit);
    enable(remove, selected != nullptr);
    for (const Entity part : {updateTitle, updateMessage, updateHint})
    {
        scene().get<UiRect>(part).visible = needsUpdate;
    }
    // The message takes the lines it wraps into, and the hint follows it.
    for (const auto& [part, value] : {std::pair{updateMessage, needsUpdate ? selected->code.message : std::string{}},
                                      std::pair{updateHint, std::string("Opening this project will rebuild its game code.")}})
    {
        scene::UiText& shown = scene().get<scene::UiText>(part);
        shown.text = value;
        if (const asset::FontData* const data = kit.fontData(shown.font))
        {
            const float height = ui::measureText(*data, value, ui::TextStyle{.size = font, .wrap = true}, sideWidth).y;
            UiRect& rect = scene().get<UiRect>(part);
            rect.offsetMax.y = rect.offsetMin.y + height + 2.0f;
        }
    }

    // The menu of a row speaks of the project it was opened on.
    const auto rowOf = [&](Entity entity) -> const Row* {
        const auto found = std::ranges::find(rows, entity, &Row::row);
        return found != rows.end() ? &*found : nullptr;
    };
    if (world.isPopupOpen(scene(), rowMenu))
    {
        if (const Row* const target = rowOf(world.contextTarget()))
        {
            const ProjectInfo* const info = infoOf(state, target->file);
            const bool exists = info != nullptr && info->exists;
            const bool update = exists && info->code.needsUpdate;
            const auto entry = std::ranges::find(state.projects.entries(), target->file, &ProjectEntry::file);
            relabel(kit, menuEdit, update ? "Update & Edit" : "Edit");
            enable(menuEdit, exists);
            enable(menuRun, exists && !update);
            enable(menuShow, exists);
            relabel(kit, menuFavorite,
                    entry != state.projects.entries().end() && entry->favorite ? "Remove from Favorites" : "Add to Favorites");
        }
        else
        {
            world.closePopup(scene(), rowMenu);
        }
    }

    // The dialogs keep what they edit in the state of the manager, which the dialog of a folder
    // may change as well.
    const std::filesystem::path parent = core::pathFromUtf8(manager.createParent);
    const std::filesystem::path directory = manager.createFolder ? parent / core::pathFromUtf8(manager.createName) : parent;
    const std::optional<std::string> problem = createProblem(manager, directory);
    if (world.isPopupOpen(scene(), createDialog))
    {
        scene().get<scene::UiText>(createStatus).text = problem ? *problem : "The project goes in " + core::toUtf8(directory);
        scene().get<UiRect>(createStatus).style = problem ? "error" : "success";
        scene().get<scene::UiImage>(createStatusIcon).texture = kit.icon(problem ? Icon::CircleX : Icon::CircleCheck);
        scene().get<UiRect>(createStatusIcon).style = problem ? "icon_error" : "icon_success";
        enable(createConfirm, !problem.has_value());
    }
    if (manager.createParent != syncedParent)
    {
        scene().get<scene::UiText>(createParent).text = manager.createParent;
        syncedParent = manager.createParent;
    }

    // The panel takes the mouse and the keys of its window, and lays itself out.
    panel.update(kit, delta, UiPanel::zoomFor(font));

    // What was done this frame.
    std::optional<std::filesystem::path> opened;
    if (world.wasClicked(settings.entity))
    {
        state.showSettings = true;
    }
    if (world.wasClicked(create.entity))
    {
        if (manager.createParent.empty() && !state.projects.entries().empty())
        {
            const auto newest = std::ranges::max_element(state.projects.entries(), {}, &ProjectEntry::lastOpened);
            manager.createParent = core::toUtf8(newest->file.parent_path().parent_path());
            scene().get<scene::UiText>(createParent).text = manager.createParent;
            syncedParent = manager.createParent;
        }
        scene().get<scene::UiText>(createName).text = manager.createName;
        world.openPopup(scene(), createDialog);
        world.startEditing(scene(), createName);
    }
    if (world.wasClicked(import.entity))
    {
        showImportDialog(state);
    }
    if (world.wasClicked(scan.entity))
    {
        showFolderDialog(state, &DialogAnswers::scanFolder);
    }
    if (world.wasChanged("sort"))
    {
        manager.sort = static_cast<ProjectSort>(std::clamp(scene().get<scene::UiDropdown>(sort).selected, 0, 2));
    }
    for (const Row& row : rows)
    {
        if (world.wasClicked(row.star))
        {
            const auto entry = std::ranges::find(state.projects.entries(), row.file, &ProjectEntry::file);
            state.projects.setFavorite(row.file, entry == state.projects.entries().end() || !entry->favorite);
            saveUserSettings(state);
        }
        else if (world.wasClicked(row.row))
        {
            manager.selected = row.file;
            if (world.wasDoubleClicked(row.row))
            {
                opened = row.file;
            }
        }
    }
    if (const Row* const target = rowOf(world.contextTarget()); target != nullptr && world.isPopupOpen(scene(), rowMenu))
    {
        manager.selected = target->file;
    }
    if (const Row* const target = rowOf(world.contextTarget()))
    {
        if (world.wasClicked(menuEdit.entity))
        {
            opened = target->file;
        }
        if (world.wasClicked(menuRun.entity))
        {
            runProject(state, target->file);
        }
        if (world.wasClicked(menuShow.entity))
        {
            static_cast<void>(state.platform.openPath(target->file.parent_path()));
        }
        if (world.wasClicked(menuFavorite.entity))
        {
            const auto entry = std::ranges::find(state.projects.entries(), target->file, &ProjectEntry::file);
            state.projects.setFavorite(target->file, entry == state.projects.entries().end() || !entry->favorite);
            saveUserSettings(state);
        }
        if (world.wasClicked(menuRemove.entity))
        {
            manager.selected = target->file;
            manager.openRemove = true;
        }
    }
    if (world.wasClicked(edit.entity))
    {
        opened = manager.selected;
    }
    if (world.wasClicked(run.entity))
    {
        runProject(state, manager.selected);
    }
    if (world.wasClicked(rename.entity) && selected != nullptr)
    {
        manager.renameBuffer = selected->name;
        manager.openRename = true;
    }
    if (world.wasClicked(show.entity))
    {
        static_cast<void>(state.platform.openPath(manager.selected.parent_path()));
    }
    if (world.wasClicked(remove.entity))
    {
        manager.openRemove = true;
    }
    if (world.wasClicked(removeMissing.entity))
    {
        if (const std::size_t removed = state.projects.removeMissing(); removed > 0)
        {
            DEVEX_LOG_INFO("Removed {} missing project{}", removed, removed == 1 ? "" : "s");
            manager.refresh = true;
            saveUserSettings(state);
        }
    }
    // Enter opens the selected project, when nothing else takes it.
    const bool dialogOpen = world.isPopupOpen(scene(), createDialog) || world.isPopupOpen(scene(), renameDialog) ||
                            world.isPopupOpen(scene(), removeDialog);
    if (panel.focused() && !dialogOpen && !world.isEditing() && state.input.pressed(platform::Key::Enter, false) &&
        !manager.selected.empty())
    {
        opened = manager.selected;
    }

    // The dialogs. Escape cancels one at once, even while its field has the keyboard, as in the
    // dialogs of ImGui.
    const bool escaped = panel.focused() && state.input.pressed(platform::Key::Escape, false);
    if (std::exchange(manager.openCreate, false))
    {
        world.openPopup(scene(), createDialog);
        world.startEditing(scene(), createName);
    }
    if (std::exchange(manager.openRename, false))
    {
        scene().get<scene::UiText>(renameName).text = manager.renameBuffer;
        world.openPopup(scene(), renameDialog);
        world.startEditing(scene(), renameName);
    }
    if (std::exchange(manager.openRemove, false))
    {
        const ProjectInfo* const info = infoOf(state, manager.selected);
        scene().get<scene::UiText>(removeText).text =
            std::format("Remove {} from the list?", info != nullptr ? info->name : std::string("the project"));
        world.openPopup(scene(), removeDialog);
    }
    if (world.isPopupOpen(scene(), createDialog))
    {
        manager.createName = scene().get<scene::UiText>(createName).text;
        manager.createParent = scene().get<scene::UiText>(createParent).text;
        syncedParent = manager.createParent;
        manager.createFolder = scene().get<scene::UiToggle>(createFolder).value;
        if (world.wasClicked(browse.entity))
        {
            showFolderDialog(state, &DialogAnswers::newProjectLocation);
        }
        if ((world.wasClicked(createConfirm.entity) || world.wasSubmitted("create")) && !problem)
        {
            world.closePopup(scene(), createDialog);
            createProject(state, directory);
        }
        else if (world.wasClicked(createCancel.entity) || world.wasCancelled() || escaped)
        {
            world.closePopup(scene(), createDialog);
        }
    }
    if (world.isPopupOpen(scene(), renameDialog))
    {
        manager.renameBuffer = scene().get<scene::UiText>(renameName).text;
        const bool valid = !manager.renameBuffer.empty();
        enable(renameConfirm, valid);
        if ((world.wasClicked(renameConfirm.entity) || world.wasSubmitted("rename")) && valid)
        {
            world.closePopup(scene(), renameDialog);
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
        else if (world.wasClicked(renameCancel.entity) || world.wasCancelled() || escaped)
        {
            world.closePopup(scene(), renameDialog);
        }
    }
    if (world.isPopupOpen(scene(), removeDialog))
    {
        if (world.wasClicked(removeConfirm.entity))
        {
            world.closePopup(scene(), removeDialog);
            state.projects.remove(manager.selected);
            manager.selected.clear();
            manager.refresh = true;
            saveUserSettings(state);
        }
        else if (world.wasClicked(removeCancel.entity) || world.wasCancelled() || escaped)
        {
            world.closePopup(scene(), removeDialog);
        }
    }

    if (opened)
    {
        openProject(state, *opened);
    }
}

void drawProjectManager(ToolsState& state)
{
    DEVEX_PROFILE_SCOPE("Project manager");
    if (!state.uiKit)
    {
        state.uiKit = std::make_shared<EditorUiKit>(state.renderer, state.icons,
                                                    state.platform.baseDirectory() / "resources" / "fonts");
    }
    if (!state.projectManagerUi)
    {
        state.projectManagerUi = std::make_shared<ProjectManagerUi>();
    }

    // The whole window, which takes the keyboard.
    state.hosts.begin("Project Manager", state.workMin, state.workMax, HostLayer::Panels);
    if (state.hosts.focusedId().empty())
    {
        state.hosts.focus("Project Manager");
    }
    state.projectManagerUi->update(state, *state.uiKit, core::Duration(state.input.delta()));
    state.hosts.end();
}

void renderProjectManager(ToolsState& state, render::RenderWorld& world)
{
    if (state.projectManagerUi && state.uiKit)
    {
        state.projectManagerUi->panel.render(*state.uiKit, world, linearColor(themeColors().outer));
    }
}

} // namespace devex::tools::detail
