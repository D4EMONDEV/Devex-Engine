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

constexpr const char* unsavedChangesPopup = "Unsaved changes";
constexpr const char* aboutPopup = "About Devex";

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
    if (menuItem(icons::FilePlus, "New Scene", "Ctrl+N", editing))
    {
        newSceneTab(state, scene);
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
    const bool hasSelection = scene.findEntity(state.selection).isValid();
    if (ImGui::BeginMenuEx("Create", icons::Plus.c_str()))
    {
        drawCreateEntityMenu(state, core::Uuid{});
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenuEx("Create Child", icons::Layers.c_str(), hasSelection))
    {
        drawCreateEntityMenu(state, state.selection);
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (menuItem(icons::Crosshair, "Frame Selection", "F", hasSelection))
    {
        frameSelection(state, scene);
    }
    if (menuItem(icons::Trash, "Delete", "Delete", hasSelection))
    {
        state.pendingCommand = makeDestroyEntityCommand(state.selection);
    }
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
        ImGui::MenuItem(viewportWindow, nullptr, &state.showViewport);
        ImGui::MenuItem(hierarchyWindow, nullptr, &state.showHierarchy);
        ImGui::MenuItem(inspectorWindow, nullptr, &state.showInspector);
        ImGui::MenuItem(assetsWindow, nullptr, &state.showAssets);
        ImGui::MenuItem(textEditorWindow, nullptr, &state.showTextEditor);
        ImGui::MenuItem(animationWindow, nullptr, &state.showAnimation);
        ImGui::MenuItem(consoleWindow, nullptr, &state.showConsole);
        ImGui::MenuItem(statisticsWindow, nullptr, &state.showStatistics);
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

[[nodiscard]] const char* presetScaleLabel(float scale)
{
    static std::string label;
    label = std::format("{:.0f} %", scale * 100.0f);
    return label.c_str();
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

    // The project in the middle, the game code and the play controls on the right.
    const std::string& projectName = state.database->project().name;
    const float nameWidth = ImGui::CalcTextSize(projectName.c_str()).x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), (ImGui::GetWindowWidth() - nameWidth) * 0.5f));
    ImGui::TextDisabled("%s", projectName.c_str());

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
        if (const std::size_t pending = state.database != nullptr ? state.database->pendingImports() : 0; pending > 0)
        {
            iconLabel(icons::Loader, colors.warning);
            ImGui::TextColored(uiColor(colors.warning), "Importing %zu asset%s", pending, pending == 1 ? "" : "s");
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

void drawSettingsWindow(ToolsState& state)
{
    if (!state.showSettings)
    {
        return;
    }
    const ImGuiViewport* const viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30.0f, ImGui::GetFontSize() * 22.0f), ImGuiCond_Appearing);
    if (!ImGui::Begin(settingsWindow, &state.showSettings, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    ThemeSettings theme = state.theme;
    ImGui::PushFont(editorFonts().bold, 0.0f);
    ImGui::SeparatorText("Theme");
    ImGui::PopFont();
    if (beginProperties("theme"))
    {
        propertyName("Preset");
        if (beginCombo("##preset", displayName(theme.preset)))
        {
            for (const ThemePreset preset : themePresets)
            {
                if (ImGui::Selectable(displayName(preset), preset == theme.preset))
                {
                    theme.applyPreset(preset);
                }
            }
            ImGui::EndCombo();
        }
        propertyName("Base color");
        ImGui::ColorEdit3("##base", &theme.baseColor[0], ImGuiColorEditFlags_DisplayHex);
        propertyName("Accent color");
        ImGui::ColorEdit3("##accent", &theme.accentColor[0], ImGuiColorEditFlags_DisplayHex);
        propertyName("Contrast");
        ImGui::SliderFloat("##contrast", &theme.contrast, -0.5f, 1.0f, "%.2f");
        endProperties();
    }

    ImGui::Spacing();
    ImGui::PushFont(editorFonts().bold, 0.0f);
    ImGui::SeparatorText("Display");
    ImGui::PopFont();
    if (beginProperties("display"))
    {
        propertyName("Interface scale");
        const float displayScale = state.window.displayScale();
        const std::string automatic = std::format("Auto ({:.0f} %)", displayScale * 100.0f);
        if (beginCombo("##scale", theme.interfaceScale > 0.0f ? presetScaleLabel(theme.interfaceScale)
                                                                     : automatic.c_str()))
        {
            if (ImGui::Selectable(automatic.c_str(), theme.interfaceScale <= 0.0f))
            {
                theme.interfaceScale = 0.0f;
            }
            for (const float scale : {0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f})
            {
                if (ImGui::Selectable(presetScaleLabel(scale), theme.interfaceScale == scale))
                {
                    theme.interfaceScale = scale;
                }
            }
            ImGui::EndCombo();
        }
        propertyName("Font size");
        ImGui::SliderFloat("##font size", &theme.fontSize, 10.0f, 22.0f, "%.0f pt");
        propertyName("Code font size");
        ImGui::SliderFloat("##code font size", &theme.codeFontSize, 10.0f, 22.0f, "%.0f pt");
        endProperties();
    }
    theme.fontSize = std::round(theme.fontSize);
    theme.codeFontSize = std::round(theme.codeFontSize);

    ImGui::Spacing();
    if (labelButton(icons::Refresh, "Reset to Defaults"))
    {
        theme = ThemeSettings{};
    }
    ImGui::End();

    if (theme != state.theme)
    {
        state.theme = theme;
        state.themeChanged = true;
        state.themeUnsaved = true;
    }
    // Saved once a drag ends rather than at every step.
    if (state.themeUnsaved && !ImGui::IsAnyItemActive())
    {
        state.themeUnsaved = false;
        saveUserSettings(state);
    }
}

void drawProjectSettingsWindow(ToolsState& state)
{
    if (!state.showProjectSettings || state.database == nullptr)
    {
        return;
    }
    const ImGuiViewport* const viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 36.0f, ImGui::GetFontSize() * 34.0f), ImGuiCond_Appearing);
    if (!ImGui::Begin("Project Settings", &state.showProjectSettings, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    const asset::Project& saved = state.database->project();
    // Edits build on those not saved yet, so that a drag keeps its progress from frame to frame.
    asset::Project project = state.pendingProject.value_or(saved);
    ImGui::PushFont(editorFonts().bold, 0.0f);
    ImGui::SeparatorText("General");
    ImGui::PopFont();
    if (beginProperties("general"))
    {
        propertyName("Name");
        ImGui::InputText("##name", &project.name);
        propertyName("Startup scene");
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", project.startupScene.empty() ? "(the first scene)" : project.startupScene.c_str());
        endProperties();
    }

    ImGui::Spacing();
    ImGui::PushFont(editorFonts().bold, 0.0f);
    ImGui::SeparatorText("Window");
    ImGui::PopFont();
    asset::WindowSettings& window = project.window;
    if (beginProperties("window"))
    {
        propertyName("Size");
        std::array<std::uint32_t, 2> size{window.width, window.height};
        const std::uint32_t minimumSize = 64;
        const std::uint32_t maximumSize = 16384;
        if (ImGui::DragScalarN("##size", ImGuiDataType_U32, size.data(), 2, 1.0f, &minimumSize, &maximumSize, "%u"))
        {
            window.width = size[0];
            window.height = size[1];
        }
        ImGui::SetItemTooltip("The size of the window, in points: the system scales it on high-density displays");
        propertyName("Fullscreen");
        ImGui::Checkbox("##fullscreen", &window.fullscreen);
        propertyName("VSync");
        ImGui::Checkbox("##vsync", &window.vsync);
        ImGui::SetItemTooltip("Waits for the display refresh: no tearing, lower power");
        propertyName("Frame rate limit");
        const std::uint32_t noLimit = 0;
        const std::uint32_t highestLimit = 1000;
        ImGui::DragScalar("##frame rate", ImGuiDataType_U32, &window.maxFrameRate, 0.5f, &noLimit, &highestLimit,
                          window.maxFrameRate == 0 ? "unlimited" : "%u fps");
        propertyName("Icon");
        static_cast<void>(drawAssetPicker(state, "##icon", asset::AssetType::Texture, window.icon));
        ImGui::SetItemTooltip("An image of the project, square and 256 pixels or more");
        endProperties();
    }

    ImGui::Spacing();
    ImGui::PushFont(editorFonts().bold, 0.0f);
    ImGui::SeparatorText("Physics");
    ImGui::PopFont();
    asset::PhysicsSettings& physics = project.physics;
    if (beginProperties("physics"))
    {
        propertyName("Gravity");
        dragVector("##gravity", &physics.gravity[0], 3, 0.05f, "%.2f");
        endProperties();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Collision layers: name the layers bodies use, then choose which ones touch.");
    if (ImGui::BeginTable("layers", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX))
    {
        ImGui::TableSetupColumn("index", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 2.0f);
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
        for (std::size_t index = 0; index < asset::physicsLayerCount; ++index)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%zu", index);
            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(static_cast<int>(index));
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##layer", index == 0 ? "Default" : "unused", &physics.layerNames[index]);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // The collision matrix of the named layers, as a triangle.
    std::vector<std::uint32_t> named;
    for (std::uint32_t index = 0; index < asset::physicsLayerCount; ++index)
    {
        if (index == 0 || !physics.layerNames[index].empty())
        {
            named.push_back(index);
        }
    }
    ImGui::Spacing();
    const ImGuiTableFlags matrixFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV |
                                        ImGuiTableFlags_HighlightHoveredColumn | ImGuiTableFlags_NoHostExtendX;
    if (ImGui::BeginTable("collisions", static_cast<int>(named.size()) + 1, matrixFlags))
    {
        ImGui::TableSetupColumn("##rows", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8.0f);
        for (auto column = named.rbegin(); column != named.rend(); ++column)
        {
            const std::string& name = physics.layerNames[*column];
            ImGui::TableSetupColumn(name.empty() ? "Default" : name.c_str(),
                                    ImGuiTableColumnFlags_AngledHeader | ImGuiTableColumnFlags_WidthFixed);
        }
        ImGui::TableAngledHeadersRow();
        for (std::size_t row = 0; row < named.size(); ++row)
        {
            const std::uint32_t layer = named[row];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(physics.layerNames[layer].empty() ? "Default" : physics.layerNames[layer].c_str());
            for (std::size_t column = 0; column < named.size() - row; ++column)
            {
                const std::uint32_t other = named[named.size() - 1 - column];
                ImGui::TableSetColumnIndex(static_cast<int>(column) + 1);
                ImGui::PushID(static_cast<int>(layer * asset::physicsLayerCount + other));
                bool collides = physics.collides(layer, other);
                if (ImGui::Checkbox("##collides", &collides))
                {
                    physics.setCollides(layer, other, collides);
                }
                ImGui::SetItemTooltip("%s and %s", physics.layerNames[layer].empty() ? "Default" : physics.layerNames[layer].c_str(),
                                      physics.layerNames[other].empty() ? "Default" : physics.layerNames[other].c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Changes apply the next time the game starts.");

    ImGui::Spacing();
    ImGui::PushFont(editorFonts().bold, 0.0f);
    ImGui::SeparatorText("Audio");
    ImGui::PopFont();
    asset::AudioSettings& audio = project.audio;
    if (beginProperties("audio"))
    {
        propertyName("Master volume");
        ImGui::SliderFloat("##master", &audio.masterVolume, 0.0f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("The volume of every sound of the game");
        endProperties();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Groups: AudioSource components and one-shot sounds play in one, whose volume code can change.");
    if (ImGui::BeginTable("audio groups", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX))
    {
        ImGui::TableSetupColumn("index", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 2.0f);
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("volume", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        for (std::size_t index = 0; index < asset::audioGroupCount; ++index)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%zu", index);
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##group", "unused", &audio.groupNames[index]);
            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderFloat("##volume", &audio.groupVolumes[index], 0.0f, 1.0f, "%.2f");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Volumes apply at once, and each time the game starts.");
    ImGui::End();

    // Saved once an edit ends, so that typing a name does not rewrite the project at every key.
    if (project.name != saved.name || project.physics != saved.physics || project.window != saved.window ||
        project.audio != saved.audio)
    {
        state.pendingProject = std::move(project);
    }
    else
    {
        state.pendingProject.reset();
    }
    if (state.pendingProject && !ImGui::IsAnyItemActive())
    {
        if (state.pendingProject->name.empty())
        {
            state.pendingProject->name = saved.name;
        }
        if (core::Result<void> written = state.database->updateProject(*std::exchange(state.pendingProject, std::nullopt)); !written)
        {
            DEVEX_LOG_ERROR("Cannot save the project: {}", written.error());
        }
    }
}

void drawEditorPopups(ToolsState& state, scene::Scene& scene)
{
    const ThemeColors& colors = themeColors();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float buttonWidth = ImGui::GetFontSize() * 7.0f;
    if (std::exchange(state.openUnsavedChangesPopup, false))
    {
        ImGui::OpenPopup(unsavedChangesPopup);
    }
    if (std::exchange(state.openAboutPopup, false))
    {
        ImGui::OpenPopup(aboutPopup);
    }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(unsavedChangesPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        const std::vector<std::size_t> tabs = tabsWithUnsavedChanges(state, scene);
        const auto textFiles = state.pendingAction ? affectedTextDocuments(state, *state.pendingAction) : std::vector<TextDocument*>{};
        const ActiveDocument live = activeDocument(state, scene);
        iconLabel(icons::TriangleAlert, colors.warning);
        ImGui::TextUnformatted("Save changes before continuing?");
        ImGui::Indent(ImGui::GetFontSize() * 1.6f);
        for (const std::size_t index : tabs)
        {
            iconLabel(icons::Clapperboard, colors.scene);
            boldText(tabName(state.tabs.path(index, live)).c_str());
        }
        for (const TextDocument* document : textFiles)
        {
            iconLabel(icons::FileText, colors.neutral);
            ImGui::TextUnformatted(core::toUtf8(document->path).c_str());
        }
        ImGui::Unindent(ImGui::GetFontSize() * 1.6f);
        ImGui::TextDisabled("Changes that are not saved are lost.");
        ImGui::Dummy(ImVec2(0.0f, style.ItemSpacing.y));

        const float width = buttonWidth * 3.0f + style.ItemSpacing.x * 2.0f;
        alignRight(width);
        if (primaryButton(icons::Save, tabs.size() + textFiles.size() > 1 ? "Save All" : "Save", buttonWidth))
        {
            ImGui::CloseCurrentPopup();
            if (saveForPendingAction(state, scene))
            {
                continuePendingAction(state, scene);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(buttonWidth, 0.0f)))
        {
            ImGui::CloseCurrentPopup();
            discardPendingAction(state, scene);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            ImGui::CloseCurrentPopup();
            cancelPendingAction(state);
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(aboutPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 3.2f);
        ImGui::TextUnformatted(icons::Logo.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::PushFont(editorFonts().bold, ImGui::GetStyle().FontSizeBase * 1.4f);
        ImGui::TextUnformatted("Devex Engine");
        ImGui::PopFont();
        ImGui::TextDisabled("Version %s, %s build", std::string(core::version()).c_str(),
                            std::string(core::buildType()).c_str());
        ImGui::TextDisabled("Open source under the MIT license");
        ImGui::EndGroup();
        ImGui::Spacing();
        ImGui::SeparatorText("Built with");
        for (const auto& [name, license] : std::array<std::pair<const char*, const char*>, 8>{{
                 {"Dear ImGui", "MIT"},
                 {"SDL 3", "zlib"},
                 {"Vulkan, volk, Vulkan Memory Allocator", "Apache 2.0, MIT"},
                 {"GLM, fastgltf, Basis Universal", "MIT, MIT, Apache 2.0"},
                 {"FreeType, plutosvg", "FreeType License, MIT"},
                 {"Noto Sans, JetBrains Mono", "SIL Open Font License 1.1"},
                 {"Lucide icons", "ISC"},
                 {"Slang", "Apache 2.0"},
             }})
        {
            ImGui::BulletText("%s", name);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", license);
        }
        ImGui::Spacing();
        alignRight(buttonWidth);
        if (ImGui::Button("Close", ImVec2(buttonWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
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
