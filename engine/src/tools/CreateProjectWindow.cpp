// The window that creates a project, as Godot's: its name, its folder, its renderer, Git, and whether it opens at once.
#include "FormUi.hpp"
#include "SettingsUi.hpp"
#include "ToolsState.hpp"

#include <devex/asset/Project.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Profiler.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace devex::tools::detail {

using scene::Entity;
using scene::UiRect;
using namespace rects;
using Button = PanelButton;

namespace {

constexpr const char* createWindow = "Create New Project";

// What each renderer is, as Godot lists it beside the choice.
constexpr std::array<std::array<std::string_view, 3>, 2> rendererNotes{{
    {"Every effect, for computers.", "Ambient occlusion, screen reflections and volumetric fog.", "Vulkan 1.3: Windows, Linux and Android."},
    {"What runs everywhere, the web included once Devex exports there.", "No ambient occlusion, screen reflections or volumetric fog.",
     "The same picture on every system."},
}};

[[nodiscard]] std::filesystem::path targetDirectory(const ProjectManagerState& manager)
{
    const std::filesystem::path parent = core::pathFromUtf8(manager.createParent);
    return manager.createFolder ? parent / core::pathFromUtf8(manager.createName) : parent;
}

// Why the new project cannot be created, or nothing.
[[nodiscard]] std::optional<std::string> createProblem(const ProjectManagerState& manager, const std::filesystem::path& directory)
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
        return manager.createFolder ? "A folder of that name is already there, and not empty." : "The project folder must be empty.";
    }
    return std::nullopt;
}

void createProject(ToolsState& state, const std::filesystem::path& directory)
{
    ProjectManagerState& manager = state.projectManager;
    core::Result<asset::Project> project = asset::createProject(directory, manager.createName);
    if (!project)
    {
        DEVEX_LOG_ERROR("Cannot create the project: {}", project.error());
        return;
    }
    if (manager.createRenderer != project->renderingMethod)
    {
        project->renderingMethod = manager.createRenderer;
        if (core::Result<void> saved = asset::saveProject(*project); !saved)
        {
            DEVEX_LOG_WARNING("Cannot keep the renderer of the project: {}", saved.error());
        }
    }
    if (manager.createGit)
    {
        if (core::Result<void> written = asset::writeGitFiles(*project); !written)
        {
            DEVEX_LOG_WARNING("Cannot write the files of Git: {}", written.error());
        }
    }
    const std::filesystem::path scenePath = project->assetsDirectory() / "scenes" / "Main.dvxscene";
    if (core::Result<void> saved = scene::saveSceneFile(makeDefaultScene(), scenePath); !saved)
    {
        DEVEX_LOG_WARNING("Cannot write the first scene: {}", saved.error());
    }
    state.projects.add(project->file);
    manager.refresh = true;
    saveUserSettings(state);
    DEVEX_LOG_INFO("Created project {} in {}", project->name, core::toUtf8(project->root));
    if (manager.editNow)
    {
        state.requests.openProject = project->file;
    }
    else
    {
        manager.selected = project->file;
    }
}

} // namespace

struct CreateProjectUi : FormUi
{
    CreateProjectUi()
        : FormUi(createProjectSurface)
    {
    }

    bool built = false;
    bool needsBuild = false;
    float builtFont = 0.0f;
    Entity name;
    Entity path;
    Entity pathState;
    Entity createFolder;
    Entity message;
    Button browse;
    std::array<Button, 2> renderers;
    std::array<Entity, 3> notes;
    Entity versionControl;
    Entity editNow;
    Button confirm;
    Button cancel;
    std::string syncedParent;

    // A box to tick, and its words beside it.
    Entity tick(Entity parent, UiRect rect, std::string_view words, bool value);
    void build(ToolsState& state, EditorUiKit& kit);
    void update(ToolsState& state, EditorUiKit& kit, core::Duration delta);
};

Entity CreateProjectUi::tick(Entity parent, UiRect rect, std::string_view words, bool value)
{
    const Entity holder = add(parent, "Tick", rect);
    const float size = std::round(font * 1.25f);
    const Entity box = add(holder, "Box",
                           UiRect{.anchorMin = {0.0f, 0.5f}, .anchorMax = {0.0f, 0.5f}, .offsetMin = {0.0f, -size * 0.5f}, .offsetMax = {size, size * 0.5f}},
                           "toggle");
    scene().add<scene::UiImage>(box);
    scene().add<scene::UiToggle>(box, scene::UiToggle{.value = value});
    scene().add<scene::UiButton>(box);
    text(holder, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {size + font * 0.45f, 0.0f}, .offsetMax = {0.0f, 0.0f}},
         std::string(words), "text");
    return box;
}

void CreateProjectUi::build(ToolsState& state, EditorUiKit& kit)
{
    const ProjectManagerState& manager = state.projectManager;
    const float row = std::round(font * 1.5f);
    const Entity root = add({}, "Create", whole());
    scene().add<scene::UiLayout>(root, scene::UiLayout{.kind = scene::UiLayoutKind::Column,
                                                       .spacing = std::round(font * 0.4f),
                                                       .padding = {font * 1.0f, font * 0.8f, font * 1.0f, font * 0.8f},
                                                       .align = scene::TextAlign::Left});
    text(root, wide(row), "Project Name:", "text");
    name = field(root, wide(line), manager.createName, "", "create");

    // Create Folder at the right of the title of the path, as Godot has it.
    const Entity pathTitle = add(root, "Path title", wide(row));
    text(pathTitle, whole(), "Project Path:", "text");
    const float folderWidth = std::round(kit.textWidth(EditorUiKit::regularFont(), "Create Folder", font) + font * 2.0f);
    createFolder = tick(pathTitle,
                        UiRect{.anchorMin = {1.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {-folderWidth, 0.0f}, .offsetMax = {0.0f, 0.0f}},
                        "Create Folder", manager.createFolder);
    tooltip(createFolder, "Makes a folder named after the project in the one chosen");
    const Entity pathLine = add(root, "Path", wide(line));
    scene().add<scene::UiLayout>(pathLine, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = std::round(font * 0.45f), .align = scene::TextAlign::Left});
    path = field(pathLine, grow(line), manager.createParent, "", "create");
    pathState = icon(kit, pathLine, middle({font * 1.1f, font * 1.1f}), Icon::CircleCheck, "icon_success");
    browse = button(kit, pathLine, Icon::FolderOpen, "Browse");
    message = text(root, wide(row), "", "success", false, scene::TextAlign::Center);

    // The renderers on the left, and what the chosen one is on the right.
    text(root, wide(row), "Renderer:", "text");
    const float noteHeight = std::round(font * 2.3f);
    const Entity renderer = add(root, "Renderer", wide(noteHeight * 3.0f + font * 0.6f));
    const float listWidth = std::round(font * 9.5f);
    const Entity choices = add(renderer, "Choices", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {listWidth, 0.0f}});
    scene().add<scene::UiLayout>(choices, scene::UiLayout{.kind = scene::UiLayoutKind::Column, .spacing = std::round(font * 0.2f), .align = scene::TextAlign::Left});
    constexpr std::array<const char*, 2> labels{"Forward+", "Compatibility"};
    for (std::size_t index = 0; index < renderers.size(); ++index)
    {
        renderers[index] = button(kit, choices, Icon::Circle, labels[index], "flat", listWidth, line, scene::TextAlign::Left);
    }
    const Entity rule = add(renderer, "Rule",
                            UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {listWidth + font * 0.6f, font * 0.2f},
                                   .offsetMax = {listWidth + font * 0.6f + 1.0f, -font * 0.2f}},
                            "separator");
    scene().add<scene::UiImage>(rule, scene::UiImage{.raycastTarget = false});
    const Entity described = add(renderer, "Notes",
                                 UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {listWidth + font * 1.4f, 0.0f}, .offsetMax = {0.0f, 0.0f}});
    scene().add<scene::UiLayout>(described, scene::UiLayout{.kind = scene::UiLayoutKind::Column, .spacing = 0.0f, .align = scene::TextAlign::Left});
    for (Entity& note : notes)
    {
        note = text(described, wide(noteHeight), "", "dim");
        scene().get<scene::UiText>(note).wrap = true;
        scene().get<scene::UiText>(note).verticalAlign = scene::TextVerticalAlign::Top;
    }
    text(root, wide(row), "It can be changed later in Project Settings > Rendering.", "dim", false, scene::TextAlign::Center);

    const Entity control = add(root, "Version control", wide(line));
    const float titleWidth = std::round(kit.textWidth(EditorUiKit::regularFont(), "Version Control Metadata:", font) + font * 0.8f);
    text(control, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {titleWidth, 0.0f}}, "Version Control Metadata:", "text");
    versionControl = choice(add(control, "Editor", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {titleWidth, 0.0f},
                                                          .offsetMax = {titleWidth + font * 7.0f, 0.0f}}),
                            {"None", "Git"});
    tooltip(control, "Git: a .gitignore that leaves out what Devex makes again by itself, and a .gitattributes for the line ends");

    add(root, "Gap", wide(std::round(font * 0.5f)));
    const float editWidth = std::round(kit.textWidth(EditorUiKit::regularFont(), "Edit Now", font) + font * 2.0f);
    editNow = tick(add(root, "Edit", wide(line)),
                   UiRect{.anchorMin = {0.5f, 0.0f}, .anchorMax = {0.5f, 1.0f}, .offsetMin = {-editWidth * 0.5f, 0.0f}, .offsetMax = {editWidth * 0.5f, 0.0f}},
                   "Edit Now", manager.editNow);
    tooltip(editNow, "Opens the project once it is made; otherwise it waits in the list");
    const Entity buttons = add(root, "Buttons", wide(line));
    scene().add<scene::UiLayout>(buttons, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = std::round(font * 4.0f), .align = scene::TextAlign::Center});
    confirm = button(kit, buttons, std::nullopt, "Create", "primary", std::round(font * 7.0f), line);
    cancel = button(kit, buttons, std::nullopt, "Cancel", "button", std::round(font * 7.0f), line);
    panel.setKeyboardNavigation(false);
}

void CreateProjectUi::update(ToolsState& state, EditorUiKit& kit, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    setFont(state.theme.fontSize);
    ProjectManagerState& manager = state.projectManager;
    if (!built || needsBuild || builtFont != font)
    {
        if (built)
        {
            std::vector<Entity> children;
            for (Entity child = scene().firstChild(panel.canvas()); child.isValid(); child = scene().nextSibling(child))
            {
                children.push_back(child);
            }
            for (const Entity child : children)
            {
                if (scene().has<scene::UiPopup>(child))
                {
                    panel.world().closePopup(scene(), child);
                }
                scene().destroyEntity(child);
            }
        }
        built = true;
        needsBuild = false;
        builtFont = font;
        build(state, kit);
        syncedParent = manager.createParent;
        panel.world().startEditing(scene(), name);
    }
    styleTooltips(colors);

    // The folder a dialog of the system chose.
    if (manager.createParent != syncedParent)
    {
        scene().get<scene::UiText>(path).text = manager.createParent;
        syncedParent = manager.createParent;
    }
    const std::filesystem::path directory = targetDirectory(manager);
    const std::optional<std::string> problem = createProblem(manager, directory);
    scene::UiText& said = scene().get<scene::UiText>(message);
    said.text = problem ? *problem : manager.createFolder ? std::format("The folder {} will be created.", manager.createName) : "The project goes in this empty folder.";
    scene().get<UiRect>(message).style = problem ? "error" : "success";
    scene().get<scene::UiImage>(pathState).texture = kit.icon(problem ? Icon::CircleX : Icon::CircleCheck);
    scene().get<UiRect>(pathState).style = problem ? "icon_error" : "icon_success";
    const std::size_t chosen = manager.createRenderer == asset::RenderingMethod::Compatibility ? 1 : 0;
    for (std::size_t index = 0; index < renderers.size(); ++index)
    {
        scene().get<scene::UiImage>(renderers[index].icon).texture = kit.icon(index == chosen ? Icon::CircleCheck : Icon::Circle);
        scene().get<UiRect>(renderers[index].icon).style = index == chosen ? "icon_accent" : "icon_dim";
        scene().get<UiRect>(renderers[index].label).style = index == chosen ? "text" : "dim";
    }
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        scene().get<scene::UiText>(notes[index]).text = std::format("• {}", rendererNotes[chosen][index]);
    }
    setChoice(versionControl, {"None", "Git"}, manager.createGit ? 1 : 0);
    enable(confirm, !problem.has_value());

    panel.update(kit, delta, UiPanel::zoomFor(font));

    const ui::UiWorld& world = panel.world();
    manager.createName = scene().get<scene::UiText>(name).text;
    manager.createParent = scene().get<scene::UiText>(path).text;
    syncedParent = manager.createParent;
    manager.createFolder = scene().get<scene::UiToggle>(createFolder).value;
    manager.editNow = scene().get<scene::UiToggle>(editNow).value;
    for (std::size_t index = 0; index < renderers.size(); ++index)
    {
        if (world.wasClicked(renderers[index].entity))
        {
            manager.createRenderer = index == 1 ? asset::RenderingMethod::Compatibility : asset::RenderingMethod::ForwardPlus;
        }
    }
    if (world.wasChanged(versionControl))
    {
        manager.createGit = scene().get<scene::UiDropdown>(versionControl).selected == 1;
    }
    if (world.wasClicked(browse.entity))
    {
        showFolderDialog(state, &DialogAnswers::newProjectLocation, core::pathFromUtf8(manager.createParent));
    }
    const std::filesystem::path chosenDirectory = targetDirectory(manager);
    const bool ready = !createProblem(manager, chosenDirectory).has_value();
    const bool escaped = panel.focused() && state.input.pressed(platform::Key::Escape, false);
    if ((world.wasClicked(confirm.entity) || world.wasSubmitted("create")) && ready)
    {
        state.showCreateProject = false;
        createProject(state, chosenDirectory);
    }
    else if (world.wasClicked(cancel.entity) || world.wasCancelled() || escaped)
    {
        state.showCreateProject = false;
    }
}

void drawCreateProjectWindow(ToolsState& state)
{
    DEVEX_PROFILE_SCOPE("Create project window");
    if (!state.showCreateProject)
    {
        return;
    }
    EditorUiKit& kit = editorUiKit(state);
    if (!state.createProjectUi)
    {
        state.createProjectUi = std::make_shared<CreateProjectUi>();
    }
    // A folder to start from: where the last project is, as the list knows it, or the Documents of the user.
    ProjectManagerState& manager = state.projectManager;
    if (manager.createParent.empty())
    {
        if (!state.projects.entries().empty())
        {
            const auto newest = std::ranges::max_element(state.projects.entries(), {}, &ProjectEntry::lastOpened);
            manager.createParent = core::toUtf8(newest->file.parent_path().parent_path());
        }
        else
        {
            manager.createParent = core::toUtf8(platform::documentsDirectory());
        }
    }
    if (beginFormWindow(state, createWindow, &state.showCreateProject, 34.0f, 28.5f))
    {
        state.createProjectUi->update(state, kit, core::Duration(state.input.delta()));
        endFormWindow(state);
    }
    // Opened again, it starts again from what the manager keeps.
    if (!state.showCreateProject)
    {
        state.createProjectUi->needsBuild = true;
    }
}

void renderCreateProjectWindow(ToolsState& state, render::RenderWorld& world)
{
    if (state.createProjectUi && state.uiKit)
    {
        state.createProjectUi->panel.render(*state.uiKit, world, linearColor(themeColors().panel));
    }
}

} // namespace devex::tools::detail
