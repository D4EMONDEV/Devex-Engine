#include "ToolsState.hpp"

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
namespace {

void logFailure(const core::Result<void>& result)
{
    if (!result)
    {
        DEVEX_LOG_WARNING("{}", result.error());
    }
}

[[nodiscard]] bool menuItem(IconText icon, const char* label, const char* shortcut = nullptr, bool enabled = true,
                            bool selected = false)
{
    return ImGui::MenuItemEx(label, icon.c_str(), shortcut, selected, enabled);
}

void openPath(ToolsState& state, const std::filesystem::path& path)
{
    logFailure(state.platform.openPath(path));
}

void drawSceneMenu(ToolsState& state, scene::Scene& scene)
{
    const bool editing = state.playState == PlayState::Editing;
    if (menuItem(icons::FilePlus, "New 2D Scene", nullptr, editing))
    {
        newSceneTab(state, scene, scene::SceneKind::TwoD);
    }
    if (menuItem(icons::FilePlus, "New 3D Scene", nullptr, editing))
    {
        newSceneTab(state, scene, scene::SceneKind::ThreeD);
    }
    if (menuItem(icons::FolderOpen, "Open Scene...", "Ctrl+O", editing))
    {
        showOpenSceneDialog(state);
    }
    ImGui::Separator();
    if (menuItem(icons::Save, "Save Scene", "Ctrl+S", editing))
    {
        static_cast<void>(saveScene(state, scene));
    }
    if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S", false, editing))
    {
        showSaveSceneDialog(state);
    }
    if (ImGui::MenuItem("Save All Scenes", "Ctrl+Alt+S", false, editing))
    {
        saveAllScenes(state, scene);
    }
    ImGui::Separator();
    // What the scene is made for, as the root of a Godot scene is a Node2D or a Node3D.
    if (ImGui::BeginMenuEx("Scene Kind", icons::Clapperboard.c_str(), editing))
    {
        for (const auto& [kind, label] : {std::pair{scene::SceneKind::TwoD, "2D: sprites, tiles and interfaces, seen from the front"},
                                          std::pair{scene::SceneKind::ThreeD, "3D: models and 2.5D, in perspective"}})
        {
            if (ImGui::MenuItem(label, nullptr, scene.kind() == kind) && scene.kind() != kind)
            {
                logFailure(state.history.execute(scene, makeSetSceneKindCommand(scene.kind(), kind)));
            }
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (menuItem(icons::Close, "Close Scene", "Ctrl+W", editing && state.tabs.active().has_value()))
    {
        requestAction(state, scene, {.kind = PendingAction::Kind::CloseTab, .tab = state.tabs.id(*state.tabs.active())});
    }
    ImGui::Separator();
    if (menuItem(icons::LogOut, "Quit to Project List", "Ctrl+Shift+Q"))
    {
        requestAction(state, scene, {.kind = PendingAction::Kind::CloseProject});
    }
    if (ImGui::MenuItem("Quit", "Ctrl+Q"))
    {
        requestAction(state, scene, {.kind = PendingAction::Kind::Quit});
    }
}

void drawEditMenu(ToolsState& state, scene::Scene& scene)
{
    const Command* const nextUndo = state.history.nextUndo();
    const std::string undoLabel = nextUndo != nullptr ? std::format("Undo {}", nextUndo->description()) : "Undo";
    if (menuItem(icons::Undo, undoLabel.c_str(), "Ctrl+Z", nextUndo != nullptr))
    {
        logFailure(state.history.undo(scene));
    }
    const Command* const nextRedo = state.history.nextRedo();
    const std::string redoLabel = nextRedo != nullptr ? std::format("Redo {}", nextRedo->description()) : "Redo";
    if (menuItem(icons::Redo, redoLabel.c_str(), "Ctrl+Y", nextRedo != nullptr))
    {
        logFailure(state.history.redo(scene));
    }
    ImGui::Separator();
    const bool hasSelection = scene.findEntity(state.selection.active()).isValid();
    if (menuItem(icons::Plus, "Create Entity..."))
    {
        openCreateEntity(state, core::Uuid{});
    }
    if (menuItem(icons::Layers, "Create Child...", nullptr, hasSelection && state.selection.size() == 1))
    {
        openCreateEntity(state, state.selection.active());
    }
    ImGui::Separator();
    if (menuItem(icons::Crosshair, "Frame Selection", "F", hasSelection))
    {
        frameSelection(state, scene);
    }
    ImGui::Separator();
    drawEntityEditMenuItems(state, scene);
}

void drawProjectMenu(ToolsState& state, scene::Scene& scene)
{
    const asset::Project& project = state.database->project();
    const bool hasCode = state.gameCode.state != GameCodeStatus::State::None;
    if (menuItem(icons::Hammer, "Build Game Code", "Ctrl+B",
                 hasCode && state.gameCode.state != GameCodeStatus::State::Building))
    {
        state.requests.buildCode = true;
    }
    if (menuItem(icons::FileCode, "Create Game Code", nullptr, !hasCode))
    {
        state.requests.createCode = true;
    }
    if (menuItem(icons::Bug, "C# Debugging...", nullptr, state.debugger.available))
    {
        state.showDebugging = true;
        ImGui::SetWindowFocus(debuggingWindow);
    }
    ImGui::Separator();
    const std::string sceneResource = project.resourcePath(state.scenePath);
    if (menuItem(icons::House, "Set Scene as Startup", nullptr, !sceneResource.empty(),
                 !sceneResource.empty() && project.startupScene == sceneResource))
    {
        asset::Project changed = project;
        changed.startupScene = sceneResource;
        if (core::Result<void> saved = state.database->updateProject(changed); !saved)
        {
            DEVEX_LOG_ERROR("Cannot save the project: {}", saved.error());
        }
        else
        {
            DEVEX_LOG_INFO("{} is the startup scene", sceneResource);
        }
    }
    if (menuItem(icons::Sliders, "Project Settings..."))
    {
        state.showProjectSettings = true;
        ImGui::SetWindowFocus("Project Settings");
    }
    if (menuItem(icons::Package, "Export Game..."))
    {
        state.showExport = true;
        ImGui::SetWindowFocus("Export Game");
    }
    ImGui::Separator();
    if (menuItem(icons::FolderOpen, "Open Project Folder"))
    {
        openPath(state, project.root);
    }
    if (menuItem(icons::Code, "Open Code Folder", nullptr, hasCode))
    {
        openPath(state, project.codeDirectory());
    }
    // Where the game keeps the saves, settings and key bindings of the player, shared with its
    // exported version: deleting it starts over as a new player.
    if (menuItem(icons::Save, "Open Player Data Folder"))
    {
        if (const core::Result<std::filesystem::path> folder = platform::userDataDirectory("", project.name))
        {
            openPath(state, *folder);
        }
        else
        {
            DEVEX_LOG_ERROR("Cannot open the folder of the player: {}", folder.error());
        }
    }
    ImGui::Separator();
    if (menuItem(icons::LogOut, "Project Manager"))
    {
        requestAction(state, scene, {.kind = PendingAction::Kind::CloseProject});
    }
}

void drawEditorMenu(ToolsState& state)
{
    if (menuItem(icons::Settings, "Editor Settings..."))
    {
        state.showSettings = true;
        ImGui::SetWindowFocus(settingsWindow);
    }
    ImGui::Separator();
    if (ImGui::BeginMenuEx("Panels", icons::LayoutDashboard.c_str()))
    {
        ImGui::MenuItem(hierarchyWindow, nullptr, &state.showHierarchy);
        ImGui::MenuItem(inspectorWindow, nullptr, &state.showInspector);
        ImGui::MenuItem(assetsWindow, nullptr, &state.showAssets);
        ImGui::MenuItem(animationWindow, nullptr, &state.showAnimation);
        ImGui::MenuItem(animatorWindow, nullptr, &state.showAnimator);
        ImGui::MenuItem(consoleWindow, nullptr, &state.showConsole);
        ImGui::MenuItem(statisticsWindow, nullptr, &state.showStatistics);
        ImGui::MenuItem(profilerWindow, nullptr, &state.showProfiler);
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Reset Layout"))
    {
        state.resetLayout = true;
    }
}

// The state of the game code: an icon in its color, with details in the tooltip.
void drawGameCodeStatus(const ToolsState& state)
{
    const ThemeColors& colors = themeColors();
    const GameCodeStatus& status = state.gameCode;
    IconText icon = icons::Code;
    ImVec4 color = colors.textDim;
    const char* label = nullptr;
    switch (status.state)
    {
    case GameCodeStatus::State::None:
        return;
    case GameCodeStatus::State::Building:
        icon = icons::Loader;
        color = colors.warning;
        label = "Compiling";
        break;
    case GameCodeStatus::State::Ready:
        icon = icons::CircleCheck;
        color = colors.success;
        label = "Code ready";
        break;
    case GameCodeStatus::State::Failed:
        icon = icons::CircleX;
        color = colors.error;
        label = "Code failed";
        break;
    }
    ImGui::AlignTextToFramePadding();
    iconLabel(icon, color);
    ImGui::TextColored(uiColor(color), "%s", label);
    if (!status.message.empty())
    {
        ImGui::SetItemTooltip("%s", status.message.c_str());
    }
}

void drawPlayControls(ToolsState& state)
{
    const ThemeColors& colors = themeColors();
    const bool editing = state.playState == PlayState::Editing;
    const bool codeReady = state.gameCode.state == GameCodeStatus::State::None ||
                           state.gameCode.state == GameCodeStatus::State::Ready;
    const char* const playHint = codeReady ? "Play the scene (F5)" : "Wait for the game code to build successfully before playing";
    if (toolButton("play", icons::Play, playHint, !editing, editing && codeReady,
                   editing ? std::optional(colors.text) : std::optional(colors.accent)))
    {
        state.requests.play = true;
        if (state.mainScreen == MainScreen::Script)
        {
            setMainScreen(state, state.camera.isTwoD() ? MainScreen::TwoD : MainScreen::ThreeD);
        }
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (toolButton("pause", icons::Pause, "Pause (F7)", state.playState == PlayState::Paused, !editing))
    {
        state.requests.togglePause = true;
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (toolButton("stop", icons::Square, "Stop (F8)", false, !editing))
    {
        state.requests.stop = true;
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (toolButton("step", icons::StepForward, "Step one fixed update (F9)", false,
                   state.playState == PlayState::Paused))
    {
        state.requests.step = true;
    }
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

namespace {

// 2D, 3D and Script in the middle of the menu bar: what the middle of the window shows.
void drawMainScreenSwitch(ToolsState& state)
{
    struct Choice
    {
        MainScreen screen;
        IconText icon;
        const char* label;
        const char* tooltip;
    };
    const std::array<Choice, 3> choices{{
        {MainScreen::TwoD, icons::Square, "2D", "2D scenes, seen from the front, and the interfaces of 3D scenes (Ctrl+F1)"},
        {MainScreen::ThreeD, icons::Cuboid, "3D", "3D scenes, 2.5D included, in perspective (Ctrl+F2)"},
        {MainScreen::Script, icons::Code, "Script", "The files of the project in the text editor"},
    }};

    const ImGuiStyle& style = ImGui::GetStyle();
    float width = 0.0f;
    for (const Choice& choice : choices)
    {
        width += ImGui::CalcTextSize(withIcon(choice.icon, choice.label).c_str()).x +
                 style.FramePadding.x * 2.0f + style.ItemSpacing.x;
    }
    ImGui::SetCursorPosX(std::max((ImGui::GetWindowWidth() - width) * 0.5f, ImGui::GetCursorPosX()));

    const ThemeColors& colors = themeColors();
    for (const Choice& choice : choices)
    {
        const bool selected = state.mainScreen == choice.screen;
        ImGui::PushStyleColor(ImGuiCol_Button, selected ? uiColor(colors.accent) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        if (ImGui::Button(withIcon(choice.icon, choice.label).c_str()))
        {
            setMainScreen(state, choice.screen);
        }
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s", choice.tooltip);
    }
}

} // namespace

void drawEditorMenus(ToolsState& state, scene::Scene& scene)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, style.FramePadding.y * 1.6f));
    const bool open = ImGui::BeginMainMenuBar();
    ImGui::PopStyleVar();
    if (!open)
    {
        return;
    }
    ImGui::TextUnformatted(icons::Logo.c_str());

    if (ImGui::BeginMenu("Scene"))
    {
        drawSceneMenu(state, scene);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit"))
    {
        drawEditMenu(state, scene);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Project"))
    {
        drawProjectMenu(state, scene);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Editor"))
    {
        drawEditorMenu(state);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help"))
    {
        if (menuItem(icons::Info, "About Devex"))
        {
            state.openAboutPopup = true;
        }
        ImGui::EndMenu();
    }

    // The project after the menus, the screens in the middle, the game code and the play controls
    // on the right.
    ImGui::TextDisabled("%s", state.database->project().name.c_str());
    drawMainScreenSwitch(state);

    const float playWidth = toolButtonWidth() * 4.0f + 6.0f;
    std::string codeLabel;
    float codeWidth = 0.0f;
    if (state.gameCode.state != GameCodeStatus::State::None)
    {
        codeWidth = ImGui::CalcTextSize(icons::Code.c_str()).x + style.ItemInnerSpacing.x +
                    ImGui::CalcTextSize("Code failed").x + style.ItemSpacing.x * 3.0f;
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - playWidth - codeWidth - style.WindowPadding.x - style.ItemSpacing.x);
    drawGameCodeStatus(state);
    ImGui::SameLine(ImGui::GetWindowWidth() - playWidth - style.WindowPadding.x);
    drawPlayControls(state);
    ImGui::EndMainMenuBar();
}

void drawStatusBar(ToolsState& state, const scene::Scene& scene)
{
    const ThemeColors& colors = themeColors();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.WindowPadding.x, style.FramePadding.y * 0.6f));
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
    const float height = ImGui::GetFrameHeight() + style.FramePadding.y * 1.2f;
    const bool open = ImGui::BeginViewportSideBar("##status bar", ImGui::GetMainViewport(), ImGuiDir_Down, height,
                                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                                      ImGuiWindowFlags_MenuBar);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (open && ImGui::BeginMenuBar())
    {
        // What loads in the background: read and decoded on the workers, then copied to the GPU.
        const std::size_t loading = (state.pendingLoads ? state.pendingLoads() : 0) + state.renderer.stats().pendingUploads;
        if (const std::size_t pending = state.database != nullptr ? state.database->pendingImports() : 0; pending > 0)
        {
            iconLabel(icons::Loader, colors.warning);
            ImGui::TextColored(uiColor(colors.warning), "Importing %zu asset%s", pending, pending == 1 ? "" : "s");
        }
        else if (loading > 0)
        {
            iconLabel(icons::Loader, colors.accent);
            ImGui::TextColored(uiColor(colors.accent), "Loading %zu asset%s", loading, loading == 1 ? "" : "s");
        }
        else if (state.gameCode.state == GameCodeStatus::State::Building)
        {
            iconLabel(icons::Hammer, colors.warning);
            ImGui::TextColored(uiColor(colors.warning), "Compiling the game code");
        }
        else
        {
            ImGui::TextDisabled("%zu entities", scene.entityCount());
        }

        const LogCounts counts = countLog(state);
        const std::string warnings = std::format("{}", counts.warnings);
        const std::string errors = std::format("{}", counts.errors);
        const float averageMilliseconds = state.frameTimes.average();
        const std::string fps =
            std::format("{:.0f} FPS", averageMilliseconds > 0.0f ? 1000.0f / averageMilliseconds : 0.0f);
        const std::string version = std::format("Devex {}", core::version());
        const float iconWidth = ImGui::CalcTextSize(icons::Info.c_str()).x + style.ItemInnerSpacing.x;
        const float width = iconWidth * 2.0f + ImGui::CalcTextSize(warnings.c_str()).x +
                            ImGui::CalcTextSize(errors.c_str()).x + ImGui::CalcTextSize(fps.c_str()).x +
                            ImGui::CalcTextSize(version.c_str()).x + style.ItemSpacing.x * 8.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - width);
        const auto counter = [&](IconText icon, ImVec4 color, const std::string& text, std::size_t count, const char* tooltip) {
            ImGui::BeginGroup();
            iconLabel(icon, count > 0 ? color : colors.textDim);
            ImGui::TextColored(uiColor(count > 0 ? color : colors.textDim), "%s", text.c_str());
            ImGui::EndGroup();
            if (ImGui::IsItemClicked())
            {
                state.showConsole = true;
                ImGui::SetWindowFocus(consoleWindow);
            }
            ImGui::SetItemTooltip("%s", tooltip);
            ImGui::SameLine(0.0f, style.ItemSpacing.x * 2.0f);
        };
        counter(icons::TriangleAlert, colors.warning, warnings, counts.warnings, "Warnings in the output");
        counter(icons::CircleX, colors.error, errors, counts.errors, "Errors in the output");
        ImGui::TextDisabled("%s", fps.c_str());
        ImGui::SameLine(0.0f, style.ItemSpacing.x * 2.0f);
        ImGui::TextDisabled("%s", version.c_str());
        ImGui::EndMenuBar();
    }
    ImGui::End();
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
        if (pressed(ImGuiMod_Ctrl | ImGuiKey_Slash) && document != nullptr)
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
        requestAction(state, scene, {.kind = PendingAction::Kind::CloseTab, .tab = state.tabs.id(*state.tabs.active())});
    }
    // Ctrl+Tab goes to the next scene tab.
    if (pressed(ImGuiMod_Ctrl | ImGuiKey_Tab) && state.tabs.size() > 1 && state.tabs.active())
    {
        activateSceneTab(state, scene, (*state.tabs.active() + 1) % state.tabs.size());
    }
}

} // namespace devex::tools::detail
