#include "EditorFrame.hpp"
#include "SettingsUi.hpp"
#include "ToolsState.hpp"

#include <devex/core/Profiler.hpp>
#include <devex/asset/Project.hpp>
#include <devex/core/BuildInfo.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <format>
#include <utility>

namespace devex::tools::detail {

using scene::Entity;
using scene::UiRect;
using namespace rects;
using Button = PanelButton;

namespace {

void logFailure(const core::Result<void>& result)
{
    if (!result)
    {
        DEVEX_LOG_WARNING("{}", result.error());
    }
}

void openPath(ToolsState& state, const std::filesystem::path& path)
{
    logFailure(state.platform.openPath(path));
}

// ---- The menus, as the entries the layer over the editor shows ----

[[nodiscard]] std::vector<MenuEntry> sceneMenu(ToolsState& state, scene::Scene& scene)
{
    const bool editing = state.playState == PlayState::Editing;
    std::vector<MenuEntry> entries;
    entries.push_back({.icon = Icon::FilePlus, .label = "New 2D Scene", .enabled = editing,
                       .action = [](ToolsState& tools, scene::Scene& edited) { newSceneTab(tools, edited, scene::SceneKind::TwoD); }});
    entries.push_back({.icon = Icon::FilePlus, .label = "New 3D Scene", .enabled = editing,
                       .action = [](ToolsState& tools, scene::Scene& edited) { newSceneTab(tools, edited, scene::SceneKind::ThreeD); }});
    entries.push_back({.icon = Icon::FolderOpen, .label = "Open Scene...", .shortcut = "Ctrl+O", .enabled = editing,
                       .action = [](ToolsState& tools, scene::Scene&) { showOpenSceneDialog(tools); }});
    entries.push_back(MenuEntry::line());
    entries.push_back({.icon = Icon::Save, .label = "Save Scene", .shortcut = "Ctrl+S", .enabled = editing,
                       .action = [](ToolsState& tools, scene::Scene& edited) { static_cast<void>(saveScene(tools, edited)); }});
    entries.push_back({.label = "Save Scene As...", .shortcut = "Ctrl+Shift+S", .enabled = editing,
                       .action = [](ToolsState& tools, scene::Scene&) { showSaveSceneDialog(tools); }});
    entries.push_back({.label = "Save All Scenes", .shortcut = "Ctrl+Alt+S", .enabled = editing,
                       .action = [](ToolsState& tools, scene::Scene& edited) { saveAllScenes(tools, edited); }});
    entries.push_back(MenuEntry::line());
    // What the scene is made for, as the root of a Godot scene is a Node2D or a Node3D.
    MenuEntry kinds{.icon = Icon::Clapperboard, .label = "Scene Kind", .enabled = editing};
    for (const auto& [kind, label] : {std::pair{scene::SceneKind::TwoD, "2D: sprites, tiles and interfaces, seen from the front"},
                                      std::pair{scene::SceneKind::ThreeD, "3D: models and 2.5D, in perspective"}})
    {
        kinds.children.push_back({.label = label, .checked = scene.kind() == kind, .action = [kind](ToolsState& tools, scene::Scene& edited) {
                                      if (edited.kind() != kind)
                                      {
                                          logFailure(tools.history.execute(edited, makeSetSceneKindCommand(edited.kind(), kind)));
                                      }
                                  }});
    }
    entries.push_back(std::move(kinds));
    entries.push_back(MenuEntry::line());
    entries.push_back({.icon = Icon::Close, .label = "Close Scene", .shortcut = "Ctrl+W", .enabled = editing && state.tabs.active().has_value(),
                       .action = [](ToolsState& tools, scene::Scene& edited) {
                           if (tools.tabs.active())
                           {
                               requestAction(tools, edited, {.kind = PendingAction::Kind::CloseTab, .tabs = {tools.tabs.id(*tools.tabs.active())}});
                           }
                       }});
    entries.push_back(MenuEntry::line());
    entries.push_back({.icon = Icon::LogOut, .label = "Quit to Project List", .shortcut = "Ctrl+Shift+Q",
                       .action = [](ToolsState& tools, scene::Scene& edited) { requestAction(tools, edited, {.kind = PendingAction::Kind::CloseProject}); }});
    entries.push_back({.label = "Quit", .shortcut = "Ctrl+Q",
                       .action = [](ToolsState& tools, scene::Scene& edited) { requestAction(tools, edited, {.kind = PendingAction::Kind::Quit}); }});
    return entries;
}

// Undo and redo, named after what they would undo and redo.
void addHistoryEntries(ToolsState& state, std::vector<MenuEntry>& entries)
{
    const Command* const nextUndo = state.history.nextUndo();
    entries.push_back({.icon = Icon::Undo, .label = nextUndo != nullptr ? std::format("Undo {}", nextUndo->description()) : std::string("Undo"),
                       .shortcut = "Ctrl+Z", .enabled = nextUndo != nullptr,
                       .action = [](ToolsState& tools, scene::Scene& edited) {
                           if (tools.history.nextUndo() != nullptr)
                           {
                               logFailure(tools.history.undo(edited));
                           }
                       }});
    const Command* const nextRedo = state.history.nextRedo();
    entries.push_back({.icon = Icon::Redo, .label = nextRedo != nullptr ? std::format("Redo {}", nextRedo->description()) : std::string("Redo"),
                       .shortcut = "Ctrl+Y", .enabled = nextRedo != nullptr,
                       .action = [](ToolsState& tools, scene::Scene& edited) {
                           if (tools.history.nextRedo() != nullptr)
                           {
                               logFailure(tools.history.redo(edited));
                           }
                       }});
}

[[nodiscard]] std::vector<MenuEntry> editMenu(ToolsState& state, scene::Scene& scene)
{
    std::vector<MenuEntry> entries;
    addHistoryEntries(state, entries);
    entries.push_back(MenuEntry::line());
    const bool hasSelection = scene.findEntity(state.selection.active()).isValid();
    entries.push_back({.icon = Icon::Plus, .label = "Create Entity...",
                       .action = [](ToolsState& tools, scene::Scene&) { openCreateEntity(tools, core::Uuid{}); }});
    if (state.mode == ToolsMode::Editor)
    {
        entries.push_back({.icon = Icon::Layers, .label = "Create Child...", .enabled = hasSelection && state.selection.size() == 1,
                           .action = [](ToolsState& tools, scene::Scene&) { openCreateEntity(tools, tools.selection.active()); }});
        entries.push_back(MenuEntry::line());
        entries.push_back({.icon = Icon::Crosshair, .label = "Frame Selection", .shortcut = "F", .enabled = hasSelection,
                           .action = [](ToolsState& tools, scene::Scene& edited) { frameSelection(tools, edited); }});
    }
    entries.push_back(MenuEntry::line());
    addEntityEditEntries(state, scene, entries);
    return entries;
}

[[nodiscard]] std::vector<MenuEntry> projectMenu(ToolsState& state)
{
    const asset::Project& project = state.database->project();
    const bool hasCode = state.gameCode.state != GameCodeStatus::State::None;
    std::vector<MenuEntry> entries;
    entries.push_back({.icon = Icon::Hammer, .label = "Build Game Code", .shortcut = "Ctrl+B",
                       .enabled = hasCode && state.gameCode.state != GameCodeStatus::State::Building,
                       .action = [](ToolsState& tools, scene::Scene&) { tools.requests.buildCode = true; }});
    entries.push_back({.icon = Icon::FileCode, .label = "Create Game Code", .enabled = !hasCode,
                       .action = [](ToolsState& tools, scene::Scene&) { tools.requests.createCode = true; }});
    entries.push_back({.icon = Icon::Bug, .label = "C# Debugging...", .enabled = state.debugger.available,
                       .action = [](ToolsState& tools, scene::Scene&) {
                           tools.showDebugging = true;
                           ImGui::SetWindowFocus(debuggingWindow);
                       }});
    entries.push_back(MenuEntry::line());
    const std::string sceneResource = project.resourcePath(state.scenePath);
    entries.push_back({.icon = Icon::House, .label = "Set Scene as Startup", .enabled = !sceneResource.empty(),
                       .checked = !sceneResource.empty() && project.startupScene == sceneResource,
                       .action = [sceneResource](ToolsState& tools, scene::Scene&) {
                           asset::Project changed = tools.database->project();
                           changed.startupScene = sceneResource;
                           if (core::Result<void> saved = tools.database->updateProject(changed); !saved)
                           {
                               DEVEX_LOG_ERROR("Cannot save the project: {}", saved.error());
                           }
                           else
                           {
                               DEVEX_LOG_INFO("{} is the startup scene", sceneResource);
                           }
                       }});
    entries.push_back({.icon = Icon::Sliders, .label = "Project Settings...", .action = [](ToolsState& tools, scene::Scene&) {
                           tools.showProjectSettings = true;
                           ImGui::SetWindowFocus("Project Settings");
                       }});
    entries.push_back({.icon = Icon::Package, .label = "Export Game...", .action = [](ToolsState& tools, scene::Scene&) {
                           tools.showExport = true;
                           ImGui::SetWindowFocus("Export Game");
                       }});
    entries.push_back(MenuEntry::line());
    entries.push_back({.icon = Icon::FolderOpen, .label = "Open Project Folder",
                       .action = [](ToolsState& tools, scene::Scene&) { openPath(tools, tools.database->project().root); }});
    entries.push_back({.icon = Icon::Code, .label = "Open Code Folder", .enabled = hasCode,
                       .action = [](ToolsState& tools, scene::Scene&) { openPath(tools, tools.database->project().codeDirectory()); }});
    // Where the game keeps the saves, settings and key bindings of the player, shared with its
    // exported version: deleting it starts over as a new player.
    entries.push_back({.icon = Icon::Save, .label = "Open Player Data Folder", .action = [](ToolsState& tools, scene::Scene&) {
                           if (const core::Result<std::filesystem::path> folder = platform::userDataDirectory("", tools.database->project().name))
                           {
                               openPath(tools, *folder);
                           }
                           else
                           {
                               DEVEX_LOG_ERROR("Cannot open the folder of the player: {}", folder.error());
                           }
                       }});
    entries.push_back(MenuEntry::line());
    entries.push_back({.icon = Icon::LogOut, .label = "Project Manager",
                       .action = [](ToolsState& tools, scene::Scene& edited) { requestAction(tools, edited, {.kind = PendingAction::Kind::CloseProject}); }});
    return entries;
}

// The panels, each shown or not.
void addPanelEntries(ToolsState& state, std::vector<MenuEntry>& entries)
{
    const auto panel = [&](const char* name, bool ToolsState::* shown) {
        entries.push_back({.label = name, .checked = state.*shown, .action = [shown](ToolsState& tools, scene::Scene&) {
                               tools.*shown = !(tools.*shown);
                               // The panels that open beside others come to the front of their dock.
                               tools.focusProfiler |= shown == &ToolsState::showProfiler && tools.showProfiler;
                               tools.focusAnimation |= shown == &ToolsState::showAnimation && tools.showAnimation;
                               tools.focusAnimator |= shown == &ToolsState::showAnimator && tools.showAnimator;
                           }});
    };
    panel(hierarchyWindow, &ToolsState::showHierarchy);
    panel(inspectorWindow, &ToolsState::showInspector);
    panel(assetsWindow, &ToolsState::showAssets);
    panel(animationWindow, &ToolsState::showAnimation);
    panel(animatorWindow, &ToolsState::showAnimator);
    panel(consoleWindow, &ToolsState::showConsole);
    panel(statisticsWindow, &ToolsState::showStatistics);
    panel(profilerWindow, &ToolsState::showProfiler);
}

[[nodiscard]] std::vector<MenuEntry> editorMenu(ToolsState& state)
{
    std::vector<MenuEntry> entries;
    entries.push_back({.icon = Icon::Settings, .label = "Editor Settings...", .action = [](ToolsState& tools, scene::Scene&) {
                           tools.showSettings = true;
                           ImGui::SetWindowFocus(settingsWindow);
                       }});
    entries.push_back(MenuEntry::line());
    MenuEntry panels{.icon = Icon::LayoutDashboard, .label = "Panels"};
    addPanelEntries(state, panels.children);
    entries.push_back(std::move(panels));
    entries.push_back({.label = "Reset Layout", .action = [](ToolsState& tools, scene::Scene&) { tools.resetLayout = true; }});
    return entries;
}

[[nodiscard]] std::vector<MenuEntry> helpMenu()
{
    return {MenuEntry{.icon = Icon::Info, .label = "About Devex", .action = [](ToolsState& tools, scene::Scene&) { tools.openAboutPopup = true; }}};
}

// The View menu of the tools over a game: its panels, and their places.
[[nodiscard]] std::vector<MenuEntry> viewMenu(ToolsState& state)
{
    std::vector<MenuEntry> entries;
    addPanelEntries(state, entries);
    entries.push_back(MenuEntry::line());
    entries.push_back({.label = "Reset Layout", .action = [](ToolsState& tools, scene::Scene&) { tools.resetLayout = true; }});
    return entries;
}

// Counts of the log messages by severity.
struct LogCounts
{
    std::size_t warnings = 0;
    std::size_t errors = 0;
};

[[nodiscard]] LogCounts countLog(const ToolsState& state)
{
    LogCounts counts;
    state.log.forEach([&counts](const LogEntry& entry) {
        if (entry.level == core::LogLevel::Warning)
        {
            ++counts.warnings;
        }
        else if (entry.level >= core::LogLevel::Error)
        {
            ++counts.errors;
        }
    });
    return counts;
}

// A strip along an edge of the window, which the dock leaves room for, its content filling it.
[[nodiscard]] bool beginStrip(const char* name, ImGuiDir edge, float height)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const bool open = ImGui::BeginViewportSideBar(name, ImGui::GetMainViewport(), edge, height,
                                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
    ImGui::PopStyleVar(2);
    return open;
}

} // namespace

const char* windowOf(MainScreen screen) noexcept
{
    return screen == MainScreen::Script ? textEditorWindow : viewportWindow;
}

std::string_view toString(MainScreen screen) noexcept
{
    switch (screen)
    {
    case MainScreen::TwoD:
        return "2D";
    case MainScreen::Script:
        return "Script";
    case MainScreen::ThreeD:
        break;
    }
    return "3D";
}

void setMainScreen(ToolsState& state, MainScreen screen)
{
    state.mainScreen = screen;
    state.mainScreenChanged = true;
    // The middle holds one screen at a time: the others close, so that none of them shows a tab.
    // 2D and 3D share the viewport, each with its own view of the scene.
    state.showViewport = screen != MainScreen::Script;
    state.showTextEditor = screen == MainScreen::Script;
    if (screen != MainScreen::Script)
    {
        state.camera.setTwoD(screen == MainScreen::TwoD);
    }
}

void showSceneScreen(ToolsState& state, const scene::Scene& scene)
{
    setMainScreen(state, scene.kind() == scene::SceneKind::TwoD ? MainScreen::TwoD : MainScreen::ThreeD);
}

// ---- The menu bar ----

// The menus at the left, then the name of the project; 2D, 3D and Script in the middle, which choose
// what the middle of the window shows; the state of the game code and the play controls at the right.
// Over a game, the tools have only their Edit and View menus.
struct MenuBarUi : PanelBuilder
{
    MenuBarUi()
        : PanelBuilder(menuBarSurface)
    {
    }

    bool built = false;
    float builtFont = 0.0f;
    ToolsMode builtMode = ToolsMode::Overlay;
    std::vector<Button> titles;
    Entity project;
    std::array<Button, 3> screens{};
    Entity codeRow;
    Entity codeIcon;
    Entity codeText;
    std::array<Button, 4> play{};

    void build(EditorUiKit& kit, ToolsMode mode);
    void sync(ToolsState& state, EditorUiKit& kit);
    [[nodiscard]] std::vector<MenuEntry> entriesOf(ToolsState& state, scene::Scene& scene, std::size_t title) const;
    void update(ToolsState& state, EditorUiKit& kit, scene::Scene& scene, core::Duration delta);
};

void MenuBarUi::build(EditorUiKit& kit, ToolsMode mode)
{
    std::vector<Entity> children;
    for (Entity child = scene().firstChild(panel.canvas()); child.isValid(); child = scene().nextSibling(child))
    {
        children.push_back(child);
    }
    for (const Entity child : children)
    {
        scene().destroyEntity(child);
    }
    built = true;
    builtFont = font;
    builtMode = mode;
    titles.clear();
    panel.setKeyboardNavigation(false);
    panel.setTooltipsOutside(true);
    const float height = std::round(font * 1.75f);
    const float iconSize = std::round(font * 1.25f);
    const Entity root = add({}, "Menu bar", whole());

    const Entity left = add(root, "Menus", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.5f, 1.0f}, .offsetMin = {font * 0.5f, 0.0f}, .offsetMax = {0.0f, 0.0f}});
    scene().add<scene::UiLayout>(left, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = 1.0f, .align = scene::TextAlign::Left});
    icon(kit, left, middle({iconSize, iconSize}), Icon::Logo, {});
    add(left, "Gap", middle({font * 0.2f, 1.0f}));
    const auto title = [&](const char* name) { titles.push_back(button(kit, left, std::nullopt, name, "bar_button", 0.0f, height)); };
    if (mode == ToolsMode::Editor)
    {
        for (const char* name : {"Scene", "Edit", "Project", "Editor", "Help"})
        {
            title(name);
        }
        add(left, "Gap", middle({font * 0.4f, 1.0f}));
        project = text(left, middle({font * 14.0f, height}), "", "dim");

        // The screens, in the middle of the bar whatever stands at its sides.
        struct Choice
        {
            Icon icon;
            const char* label;
            const char* tooltip;
        };
        const std::array<Choice, 3> choices{{
            {Icon::Square, "2D", "2D scenes, seen from the front, and the interfaces of 3D scenes (Ctrl+F1)"},
            {Icon::Cuboid, "3D", "3D scenes, 2.5D included, in perspective (Ctrl+F2)"},
            {Icon::Code, "Script", "The files of the project in the text editor (Ctrl+F3)"},
        }};
        const Entity middleRow = add(root, "Screens", UiRect{.anchorMin = {0.5f, 0.0f}, .anchorMax = {0.5f, 1.0f}});
        scene().add<scene::UiLayout>(middleRow, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = 2.0f, .align = scene::TextAlign::Left});
        float width = 0.0f;
        for (std::size_t index = 0; index < choices.size(); ++index)
        {
            screens[index] = button(kit, middleRow, choices[index].icon, choices[index].label, "bar_button", 0.0f, height);
            tooltip(screens[index].entity, choices[index].tooltip);
            const UiRect& rect = scene().get<UiRect>(screens[index].entity);
            width += rect.offsetMax.x - rect.offsetMin.x + (index > 0 ? 2.0f : 0.0f);
        }
        UiRect& placed = scene().get<UiRect>(middleRow);
        placed.offsetMin = {-width * 0.5f, 0.0f};
        placed.offsetMax = {width * 0.5f, 0.0f};
    }
    else
    {
        title("Edit");
        title("View");
    }

    const Entity right = add(root, "Play", UiRect{.anchorMin = {0.5f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {-font * 0.5f, 0.0f}});
    scene().add<scene::UiLayout>(right, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = 2.0f, .align = scene::TextAlign::Right});
    if (mode == ToolsMode::Editor)
    {
        codeRow = add(right, "Code", middle({font * 8.0f, height}));
        codeIcon = icon(kit, codeRow, UiRect{.anchorMin = {0.0f, 0.5f}, .anchorMax = {0.0f, 0.5f}, .offsetMin = {0.0f, -iconSize * 0.5f},
                                             .offsetMax = {iconSize, iconSize * 0.5f}},
                        Icon::Code, {});
        codeText = text(codeRow, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {iconSize + font * 0.4f, 0.0f}, .offsetMax = {0.0f, 0.0f}},
                        "", {});
        scene().add<scene::UiImage>(codeRow, scene::UiImage{.color = {0.0f, 0.0f, 0.0f, 0.0f}});
        const float tool = std::round(font * 1.7f);
        const std::array<Icon, 4> glyphs{Icon::Play, Icon::Pause, Icon::Square, Icon::StepForward};
        for (std::size_t index = 0; index < play.size(); ++index)
        {
            play[index].entity = add(right, "Button", middle({tool, tool}), "bar_button");
            scene().add<scene::UiImage>(play[index].entity);
            scene().add<scene::UiButton>(play[index].entity);
            play[index].icon = icon(kit, play[index].entity, whole(math::Vec4{std::round(tool * 0.22f)}), glyphs[index], "icon");
        }
    }
    else
    {
        text(right, middle({font * 10.0f, height}), "F1 hides the tools", "dim", false, scene::TextAlign::Right);
    }
}

std::vector<MenuEntry> MenuBarUi::entriesOf(ToolsState& state, scene::Scene& scene, std::size_t title) const
{
    if (builtMode != ToolsMode::Editor)
    {
        return title == 0 ? editMenu(state, scene) : viewMenu(state);
    }
    switch (title)
    {
    case 0:
        return sceneMenu(state, scene);
    case 1:
        return editMenu(state, scene);
    case 2:
        return projectMenu(state);
    case 3:
        return editorMenu(state);
    default:
        return helpMenu();
    }
}

void MenuBarUi::update(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    if (!built || builtFont != state.theme.fontSize || builtMode != state.mode)
    {
        font = state.theme.fontSize;
        build(kit, state.mode);
    }
    styleTooltips(colors);
    const bool editor = builtMode == ToolsMode::Editor;
    const bool editing = state.playState == PlayState::Editing;
    const std::optional<std::size_t> open = editorMenuOwner(state);

    for (std::size_t index = 0; index < titles.size(); ++index)
    {
        scene().get<UiRect>(titles[index].entity).style = barStyle(panel.world(), titles[index].entity, open == index);
    }
    if (editor)
    {
        sync(state, kit);
    }

    panel.update(kit, delta, UiPanel::zoomFor(font));
    const ui::UiWorld& world = panel.world();

    // A title opens its menu under itself, or closes it; while one is open, the pointer on another
    // title goes to its menu, as in every menu bar.
    const ui::LayoutResult* const layout = world.canvases().empty() ? nullptr : &world.canvases().front().layout;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    for (std::size_t index = 0; index < titles.size() && layout != nullptr; ++index)
    {
        const ui::LaidOutRect* const rect = layout->find(titles[index].entity);
        if (rect == nullptr)
        {
            continue;
        }
        const ImVec2 min = panel.screenOf(rect->min);
        const ImVec2 max = panel.screenOf(rect->max);
        const bool pointed = mouse.x >= min.x && mouse.x < max.x && mouse.y >= min.y && mouse.y < max.y;
        const bool clicked = world.wasClicked(titles[index].entity);
        if ((clicked && open != index) || (open && *open != index && pointed) || state.menuRequest == index)
        {
            // Under the bar, at the left of its title.
            openEditorMenu(state, entriesOf(state, edited, index), panel.screenOf(math::Vec2{rect->min.x, panel.size().y}), index);
        }
    }
    if (layout != nullptr)
    {
        state.menuRequest.reset();
    }
    if (!editor)
    {
        return;
    }

    const std::array<MainScreen, 3> shown{MainScreen::TwoD, MainScreen::ThreeD, MainScreen::Script};
    for (std::size_t index = 0; index < screens.size(); ++index)
    {
        if (world.wasClicked(screens[index].entity))
        {
            setMainScreen(state, shown[index]);
        }
    }
    const bool codeReady = state.gameCode.state == GameCodeStatus::State::None || state.gameCode.state == GameCodeStatus::State::Ready;
    if (world.wasClicked(play[0].entity) && editing && codeReady)
    {
        state.requests.play = true;
        if (state.mainScreen == MainScreen::Script)
        {
            setMainScreen(state, state.camera.isTwoD() ? MainScreen::TwoD : MainScreen::ThreeD);
        }
    }
    if (world.wasClicked(play[1].entity) && !editing)
    {
        state.requests.togglePause = true;
    }
    if (world.wasClicked(play[2].entity) && !editing)
    {
        state.requests.stop = true;
    }
    if (world.wasClicked(play[3].entity) && state.playState == PlayState::Paused)
    {
        state.requests.step = true;
    }
}

// What the bar of the editor shows of the project, of the screens, of the code and of the game.
void MenuBarUi::sync(ToolsState& state, EditorUiKit& kit)
{
    const ThemeColors& colors = themeColors();
    MenuBarUi& bar = *this;
    scene::Scene& scene = bar.scene();
    const bool editing = state.playState == PlayState::Editing;
    scene.get<scene::UiText>(bar.project).text = state.database != nullptr ? state.database->project().name : std::string{};

    const std::array<MainScreen, 3> shown{MainScreen::TwoD, MainScreen::ThreeD, MainScreen::Script};
    for (std::size_t index = 0; index < bar.screens.size(); ++index)
    {
        scene.get<UiRect>(bar.screens[index].entity).style =
            state.mainScreen == shown[index] ? "primary" : barStyle(panel.world(), bar.screens[index].entity, false);
    }

    // The state of the game code: an icon in its colour, with details in the tooltip.
    const GameCodeStatus& status = state.gameCode;
    Icon glyph = Icon::Code;
    ImVec4 color = colors.textDim;
    const char* label = "";
    switch (status.state)
    {
    case GameCodeStatus::State::None:
        break;
    case GameCodeStatus::State::Building:
        glyph = Icon::Loader;
        color = colors.warning;
        label = "Compiling";
        break;
    case GameCodeStatus::State::Ready:
        glyph = Icon::CircleCheck;
        color = colors.success;
        label = "Code ready";
        break;
    case GameCodeStatus::State::Failed:
        glyph = Icon::CircleX;
        color = colors.error;
        label = "Code failed";
        break;
    }
    UiRect& code = scene.get<UiRect>(bar.codeRow);
    code.visible = status.state != GameCodeStatus::State::None;
    scene.get<scene::UiImage>(bar.codeIcon).texture = kit.icon(glyph);
    scene.get<scene::UiImage>(bar.codeIcon).color = linearColor(color);
    scene::UiText& codeLabel = scene.get<scene::UiText>(bar.codeText);
    codeLabel.text = label;
    codeLabel.color = linearColor(color);
    code.offsetMax.x = code.offsetMin.x + bar.font * 1.9f + kit.textWidth(EditorUiKit::regularFont(), label, bar.font) + bar.font * 0.8f;
    bar.tooltip(bar.codeRow, status.message);

    // Play, pause, stop and step, each lit or dimmed as the game stands.
    const bool codeReady = status.state == GameCodeStatus::State::None || status.state == GameCodeStatus::State::Ready;
    const bool paused = state.playState == PlayState::Paused;
    const std::array<bool, 4> enabled{editing && codeReady, !editing, !editing, paused};
    const std::array<bool, 4> lit{!editing, paused, false, false};
    const std::array<const char*, 4> hints{codeReady ? "Play the scene (F5)" : "Wait for the game code to build successfully before playing",
                                           "Pause (F7)", "Stop (F8)", "Step one fixed update (F9)"};
    for (std::size_t index = 0; index < bar.play.size(); ++index)
    {
        scene.get<UiRect>(bar.play[index].entity).style = barStyle(panel.world(), bar.play[index].entity, lit[index]);
        // The play button stays lit while the game runs, though it cannot be pressed again.
        bar.enable(bar.play[index], enabled[index] || lit[index]);
        scene.get<UiRect>(bar.play[index].icon).style = lit[index] ? "icon_accent" : "icon";
        bar.tooltip(bar.play[index].entity, hints[index]);
    }
}

void drawEditorMenus(ToolsState& state, scene::Scene& scene)
{
    DEVEX_PROFILE_SCOPE("Menus");
    EditorUiKit& kit = editorUiKit(state);
    const ImGuiStyle& style = ImGui::GetStyle();
    if (beginStrip("##menu bar", ImGuiDir_Up, ImGui::GetFontSize() + style.FramePadding.y * 3.2f))
    {
        if (!state.menuBarUi)
        {
            state.menuBarUi = std::make_shared<MenuBarUi>();
        }
        state.menuBarUi->update(state, kit, scene, core::Duration(ImGui::GetIO().DeltaTime));
    }
    ImGui::End();
}

// ---- The status bar ----

// What happens in the background at the left; the warnings and the errors of the output, the frames a
// second and the version at the right.
struct StatusBarUi : PanelBuilder
{
    StatusBarUi()
        : PanelBuilder(statusBarSurface)
    {
    }

    bool built = false;
    float builtFont = 0.0f;
    Entity stateIcon;
    Entity stateText;
    Button warnings;
    Button errors;
    Entity fps;
    Entity version;

    void build(EditorUiKit& kit);
    void update(ToolsState& state, EditorUiKit& kit, const scene::Scene& scene, core::Duration delta);
};

void StatusBarUi::build(EditorUiKit& kit)
{
    std::vector<Entity> children;
    for (Entity child = scene().firstChild(panel.canvas()); child.isValid(); child = scene().nextSibling(child))
    {
        children.push_back(child);
    }
    for (const Entity child : children)
    {
        scene().destroyEntity(child);
    }
    built = true;
    builtFont = font;
    panel.setKeyboardNavigation(false);
    panel.setTooltipsOutside(true);
    const float height = std::round(font * 1.5f);
    const float iconSize = std::round(font * 1.05f);
    const Entity root = add({}, "Status bar", whole());
    const Entity left = add(root, "State", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.6f, 1.0f}, .offsetMin = {font * 0.7f, 0.0f}, .offsetMax = {0.0f, 0.0f}});
    scene().add<scene::UiLayout>(left, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = font * 0.4f, .align = scene::TextAlign::Left});
    stateIcon = icon(kit, left, middle({iconSize, iconSize}), Icon::Loader, {});
    stateText = text(left, middle({font * 24.0f, height}), "", {});

    const Entity right = add(root, "Counts", UiRect{.anchorMin = {0.6f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {-font * 0.7f, 0.0f}});
    scene().add<scene::UiLayout>(right, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = font * 0.8f, .align = scene::TextAlign::Right});
    warnings = button(kit, right, Icon::TriangleAlert, "0000", "bar_button", 0.0f, height);
    tooltip(warnings.entity, "Warnings in the output");
    errors = button(kit, right, Icon::CircleX, "0000", "bar_button", 0.0f, height);
    tooltip(errors.entity, "Errors in the output");
    fps = text(right, middle({font * 4.5f, height}), "", "dim", false, scene::TextAlign::Right);
    const std::string engine = std::format("Devex {}", core::version());
    version = text(right, middle({std::ceil(kit.textWidth(EditorUiKit::regularFont(), engine, font)) + 2.0f, height}), engine, "dim", false,
                   scene::TextAlign::Right);
}

void StatusBarUi::update(ToolsState& state, EditorUiKit& kit, const scene::Scene& edited, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    if (!built || builtFont != state.theme.fontSize)
    {
        font = state.theme.fontSize;
        build(kit);
    }
    styleTooltips(colors);

    // What loads in the background: read and decoded on the workers, then copied to the GPU.
    const std::size_t loading = (state.pendingLoads ? state.pendingLoads() : 0) + state.renderer.stats().pendingUploads;
    const std::size_t pending = state.database != nullptr ? state.database->pendingImports() : 0;
    std::string message;
    ImVec4 color = colors.textDim;
    std::optional<Icon> glyph;
    if (pending > 0)
    {
        message = std::format("Importing {} asset{}", pending, pending == 1 ? "" : "s");
        color = colors.warning;
        glyph = Icon::Loader;
    }
    else if (loading > 0)
    {
        message = std::format("Loading {} asset{}", loading, loading == 1 ? "" : "s");
        color = colors.accent;
        glyph = Icon::Loader;
    }
    else if (state.gameCode.state == GameCodeStatus::State::Building)
    {
        message = "Compiling the game code";
        color = colors.warning;
        glyph = Icon::Hammer;
    }
    else
    {
        message = std::format("{} entities", edited.entityCount());
    }
    scene().get<UiRect>(stateIcon).visible = glyph.has_value();
    scene().get<scene::UiImage>(stateIcon).texture = kit.icon(glyph.value_or(Icon::Loader));
    scene().get<scene::UiImage>(stateIcon).color = linearColor(color);
    scene::UiText& shownState = scene().get<scene::UiText>(stateText);
    shownState.text = std::move(message);
    shownState.color = linearColor(color);

    const LogCounts counts = countLog(state);
    const auto counter = [&](const Button& target, std::size_t count, ImVec4 lit) {
        relabel(kit, target, std::format("{}", count));
        scene().get<UiRect>(target.entity).style = barStyle(panel.world(), target.entity, false);
        const math::Vec4 tint = linearColor(count > 0 ? lit : colors.textDim);
        scene().get<scene::UiImage>(target.icon).color = tint;
        scene().get<UiRect>(target.icon).style = {};
        scene().get<scene::UiText>(target.label).color = tint;
        scene().get<UiRect>(target.label).style = {};
    };
    counter(warnings, counts.warnings, colors.warning);
    counter(errors, counts.errors, colors.error);
    const float averageMilliseconds = state.frameTimes.average();
    scene().get<scene::UiText>(fps).text = std::format("{:.0f} FPS", averageMilliseconds > 0.0f ? 1000.0f / averageMilliseconds : 0.0f);

    panel.update(kit, delta, UiPanel::zoomFor(font));
    const ui::UiWorld& world = panel.world();
    if (world.wasClicked(warnings.entity) || world.wasClicked(errors.entity))
    {
        focusPanel(state, consoleWindow);
    }
}

void drawStatusBar(ToolsState& state, const scene::Scene& scene)
{
    DEVEX_PROFILE_SCOPE("Status bar");
    EditorUiKit& kit = editorUiKit(state);
    const ImGuiStyle& style = ImGui::GetStyle();
    if (beginStrip("##status bar", ImGuiDir_Down, ImGui::GetFrameHeight() + style.FramePadding.y * 1.2f))
    {
        if (!state.statusBarUi)
        {
            state.statusBarUi = std::make_shared<StatusBarUi>();
        }
        state.statusBarUi->update(state, kit, scene, core::Duration(ImGui::GetIO().DeltaTime));
    }
    ImGui::End();
}

void renderEditorFrame(ToolsState& state, render::RenderWorld& world)
{
    if (!state.uiKit)
    {
        return;
    }
    const math::Vec4 outer = linearColor(themeColors().outer);
    if (state.menuBarUi)
    {
        state.menuBarUi->panel.render(*state.uiKit, world, outer);
    }
    if (state.statusBarUi)
    {
        state.statusBarUi->panel.render(*state.uiKit, world, outer);
    }
    renderViewportHeader(state, world);
    renderEditorLayer(state, world);
}

void handleEditorShortcuts(ToolsState& state, scene::Scene& scene)
{
    const bool editing = state.playState == PlayState::Editing;
    const ImGuiInputFlags global = ImGuiInputFlags_RouteGlobal;
    const auto pressed = [global](ImGuiKeyChord chord) { return ImGui::Shortcut(chord, global); };
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_P))
    {
        (editing ? state.requests.play : state.requests.stop) = true;
    }
    if (pressed(ImGuiKey_F5) && editing)
    {
        state.requests.play = true;
        showSceneScreen(state, scene);
    }
    if ((pressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P) || pressed(ImGuiKey_F7)) && !editing)
    {
        state.requests.togglePause = true;
    }
    if (pressed(ImGuiKey_F8) && !editing)
    {
        state.requests.stop = true;
    }
    if ((pressed(ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_P) || pressed(ImGuiKey_F9)) &&
        state.playState == PlayState::Paused)
    {
        state.requests.step = true;
    }
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_B) && state.gameCode.state != GameCodeStatus::State::None)
    {
        state.requests.buildCode = true;
    }
    if (pressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Q))
    {
        requestAction(state, scene, {.kind = PendingAction::Kind::CloseProject});
    }
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_Q))
    {
        requestAction(state, scene, {.kind = PendingAction::Kind::Quit});
    }
    // The screens of the menu bar, numbered as Godot numbers them.
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_F1))
    {
        setMainScreen(state, MainScreen::TwoD);
    }
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_F2))
    {
        setMainScreen(state, MainScreen::ThreeD);
    }
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_F3))
    {
        setMainScreen(state, MainScreen::Script);
    }
    if (textEditorFocused())
    {
        if (pressed(ImGuiMod_Ctrl | ImGuiKey_S))
        {
            if (TextDocument* document = findTextDocument(state, state.activeText))
            {
                static_cast<void>(saveTextFile(state, scene, *document));
            }
        }
        if (pressed(ImGuiMod_Ctrl | ImGuiKey_O))
        {
            showOpenTextDialog(state);
        }
        if (pressed(ImGuiMod_Ctrl | ImGuiKey_W) && !state.activeText.empty())
        {
            requestAction(state, scene, {.kind = PendingAction::Kind::CloseText, .path = state.activeText});
        }
        TextDocument* const document = findTextDocument(state, state.activeText);
        if (pressed(ImGuiMod_Ctrl | ImGuiKey_F) || pressed(ImGuiMod_Ctrl | ImGuiKey_H))
        {
            state.textEdit.showFind = true;
            state.textEdit.showReplace = state.textEdit.showReplace || ImGui::IsKeyDown(ImGuiKey_H);
            state.textEdit.focusFind = true;
        }
        if (pressed(ImGuiMod_Ctrl | ImGuiKey_G))
        {
            state.textEdit.openGoTo = true;
        }
        // Ctrl+K as in Godot, on every keyboard; Ctrl+/ too where / is a key of its own, as on the
        // keypad.
        if ((pressed(ImGuiMod_Ctrl | ImGuiKey_K) || pressed(ImGuiMod_Ctrl | ImGuiKey_Slash) || pressed(ImGuiMod_Ctrl | ImGuiKey_KeypadDivide)) &&
            document != nullptr)
        {
            commentSelection(state.textEdit, *document, languageOf(document->path));
        }
        if (pressed(ImGuiKey_F3) && document != nullptr)
        {
            selectMatch(state.textEdit, *document, ImGui::GetIO().KeyShift ? -1 : 1);
        }
        return;
    }
    if (!editing)
    {
        return;
    }
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_S))
    {
        static_cast<void>(saveScene(state, scene));
    }
    if (pressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S))
    {
        showSaveSceneDialog(state);
    }
    if (pressed(ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_S))
    {
        saveAllScenes(state, scene);
    }
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_N))
    {
        newSceneTab(state, scene);
    }
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_O))
    {
        showOpenSceneDialog(state);
    }
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_W) && state.tabs.active())
    {
        requestAction(state, scene, {.kind = PendingAction::Kind::CloseTab, .tabs = {state.tabs.id(*state.tabs.active())}});
    }
    // Ctrl+Tab goes to the next scene tab.
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_Tab) && state.tabs.size() > 1 && state.tabs.active())
    {
        activateSceneTab(state, scene, (*state.tabs.active() + 1) % state.tabs.size());
    }
}

} // namespace devex::tools::detail
