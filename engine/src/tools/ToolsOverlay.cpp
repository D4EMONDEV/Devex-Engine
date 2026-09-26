#include "ToolsState.hpp"

#include <devex/asset/Artifact.hpp>
#include <devex/core/Assert.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Profiler.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/ModelInstantiation.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/tools/SceneCommands.hpp>
#include <devex/tools/ToolsOverlay.hpp>

#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <format>
#include <utility>

namespace devex::tools {
namespace detail {

void FrameTimes::record(float milliseconds) noexcept
{
    m_milliseconds[m_next] = milliseconds;
    m_next = (m_next + 1) % m_milliseconds.size();
    m_count = std::min(m_count + 1, m_milliseconds.size());
}

float FrameTimes::average() const noexcept
{
    if (m_count == 0)
    {
        return 0.0f;
    }
    float sum = 0.0f;
    for (std::size_t index = 0; index < m_count; ++index)
    {
        sum += m_milliseconds[index];
    }
    return sum / static_cast<float>(m_count);
}

float FrameTimes::maximum() const noexcept
{
    return *std::max_element(m_milliseconds.begin(), m_milliseconds.begin() + static_cast<std::ptrdiff_t>(std::max<std::size_t>(m_count, 1)));
}

const float* FrameTimes::values() const noexcept
{
    return m_milliseconds.data();
}

int FrameTimes::count() const noexcept
{
    return static_cast<int>(m_count);
}

int FrameTimes::offset() const noexcept
{
    return m_count < m_milliseconds.size() ? 0 : static_cast<int>(m_next);
}

core::Uuid uuidFromBytes(const std::array<std::uint8_t, 16>& bytes) noexcept
{
    std::uint64_t high = 0;
    std::uint64_t low = 0;
    for (std::size_t index = 0; index < 8; ++index)
    {
        high = high << 8 | bytes[index];
        low = low << 8 | bytes[8 + index];
    }
    return core::Uuid::fromParts(high, low);
}

void dragAsset(asset::AssetId id, asset::AssetType type, const std::string& label)
{
    if (ImGui::BeginDragDropSource())
    {
        const AssetPayload payload{.uuid = id.uuid.bytes(), .type = type};
        ImGui::SetDragDropPayload(assetPayload, &payload, sizeof(payload));
        ImGui::Text("%s (%s)", label.c_str(), std::string(asset::toString(type)).c_str());
        ImGui::EndDragDropSource();
    }
}

std::optional<asset::AssetId> acceptDroppedAsset(std::optional<asset::AssetType> type)
{
    std::optional<asset::AssetId> dropped;
    if (ImGui::BeginDragDropTarget())
    {
        // Peeked first, so that assets of another type are not highlighted as accepted.
        const ImGuiPayload* const peeked = ImGui::GetDragDropPayload();
        AssetPayload payload;
        bool matches = false;
        if (peeked != nullptr && peeked->IsDataType(assetPayload) && peeked->DataSize == sizeof(AssetPayload))
        {
            std::memcpy(&payload, peeked->Data, sizeof(payload));
            matches = !type || payload.type == *type;
        }
        if (matches && ImGui::AcceptDragDropPayload(assetPayload) != nullptr)
        {
            dropped = asset::AssetId{uuidFromBytes(payload.uuid)};
        }
        ImGui::EndDragDropTarget();
    }
    return dropped;
}

void requestInstantiateModel(ToolsState& state, asset::AssetId model, core::Uuid parent,
                             std::optional<math::Vec3> position)
{
    const asset::AssetInfo* const info = state.database != nullptr ? state.database->find(model) : nullptr;
    if (info == nullptr || info->type != asset::AssetType::Model)
    {
        return;
    }
    const core::Result<std::vector<std::byte>> bytes = state.database->loadArtifact(model);
    const core::Result<asset::ModelData> data =
        bytes ? asset::decodeModel(*bytes) : core::Result<asset::ModelData>(std::unexpected(bytes.error()));
    if (!data)
    {
        DEVEX_LOG_WARNING("Cannot place model {}: {}", info->name, data.error());
        return;
    }

    // Built in a scratch scene, then applied as one undoable step with fixed UUIDs.
    scene::Scene scratch;
    const scene::Entity root = scene::instantiateModel(scratch, *data, info->name);
    if (scene::Transform* const transform = scratch.tryGet<scene::Transform>(root); transform != nullptr && position)
    {
        transform->position = *position;
    }
    const core::Uuid rootUuid = scratch.uuid(root);
    state.pendingCommand = makeCreateEntityTreeCommand(scene::saveEntityTree(scratch, root), rootUuid, parent,
                                                       std::format("Place {}", info->name));
    state.selection.set(rootUuid);
}

std::string sceneAssetName(const ToolsState& state, asset::AssetId sceneAsset)
{
    const std::optional<asset::SourceFile> source =
        state.database != nullptr ? state.database->sourceOf(sceneAsset) : std::nullopt;
    return source ? core::toUtf8(core::pathFromUtf8(source->path).stem()) : sceneAsset.uuid.toString();
}

void requestInstantiatePrefab(ToolsState& state, asset::AssetId prefab, core::Uuid parent,
                              std::optional<math::Vec3> position)
{
    if (state.database == nullptr)
    {
        return;
    }
    const std::string name = sceneAssetName(state, prefab);
    const std::string edited = state.scenePath.empty() ? std::string() : state.database->project().resourcePath(state.scenePath);
    if (const std::optional<asset::AssetId> current = edited.empty() ? std::nullopt : state.database->findByPath(edited);
        current && scene::prefabUses(prefab, *current))
    {
        DEVEX_LOG_WARNING("Cannot place {} here: a scene cannot contain itself", name);
        return;
    }

    // Built in a scratch scene, then applied as one undoable step with fixed UUIDs.
    scene::Scene scratch;
    const core::Result<scene::Entity> root = scene::instantiatePrefab(scratch, prefab);
    if (!root)
    {
        DEVEX_LOG_WARNING("Cannot place {}: {}", name, root.error());
        return;
    }
    if (scene::Transform* const transform = scratch.tryGet<scene::Transform>(*root); transform != nullptr && position)
    {
        transform->position = *position;
    }
    const core::Uuid rootUuid = scratch.uuid(*root);
    state.pendingCommand = makeCreateEntityTreeCommand(scene::saveEntityTree(scratch, *root), rootUuid, parent,
                                                       std::format("Place {}", name));
    state.selection.set(rootUuid);
}

void requestCreateEntity(ToolsState& state, core::Uuid parent)
{
    const core::Uuid entity = core::Uuid::generate();
    state.pendingCommand = makeCreateEntityCommand(entity, "Entity", parent);
    state.selection.set(entity);
}

std::string displayName(std::string_view identifier)
{
    std::string name(identifier);
    std::ranges::replace(name, '_', ' ');
    if (!name.empty())
    {
        name.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(name.front())));
    }
    return name;
}

} // namespace detail

namespace {

using detail::ToolsState;

void logFailure(const core::Result<void>& result)
{
    if (!result)
    {
        DEVEX_LOG_WARNING("{}", result.error());
    }
}

void undo(ToolsState& state, scene::Scene& scene)
{
    if (state.history.nextUndo() != nullptr)
    {
        logFailure(state.history.undo(scene));
    }
}

void redo(ToolsState& state, scene::Scene& scene)
{
    if (state.history.nextRedo() != nullptr)
    {
        logFailure(state.history.redo(scene));
    }
}

// The theme applies between frames, when it changed or the window moved to a display of another scale.
void refreshTheme(ToolsState& state)
{
    const float displayScale = state.window.displayScale();
    if (!state.themeChanged && displayScale == state.appliedDisplayScale)
    {
        return;
    }
    state.themeChanged = false;
    state.appliedDisplayScale = displayScale;
    detail::applyTheme(state.theme, displayScale, state.renderer.imGuiNeedsLinearColors());
    const detail::ThemeColors& colors = detail::themeColors();
    state.window.setTitleBarColors(colors.dark, math::Vec3(colors.outer.x, colors.outer.y, colors.outer.z));
}

// The project manager takes a compact window; the editor takes the whole screen.
void applyWindowLayout(ToolsState& state, detail::WindowLayout layout)
{
    if (state.windowLayout == layout)
    {
        return;
    }
    const bool first = state.windowLayout == detail::WindowLayout::Unset;
    state.windowLayout = layout;
    // Hidden windows, as in tests, keep their size.
    if (state.window.isHidden())
    {
        return;
    }
    if (layout == detail::WindowLayout::Editor)
    {
        state.window.maximize();
    }
    else if (!first || !state.window.isMaximized())
    {
        const float scale = state.window.displayScale();
        state.window.setSize({static_cast<std::uint32_t>(1160.0f * scale), static_cast<std::uint32_t>(820.0f * scale)});
        state.window.center();
    }
}

void drawOverlayMenu(ToolsState& state, scene::Scene& scene)
{
    if (!ImGui::BeginMainMenuBar())
    {
        return;
    }
    ImGui::TextUnformatted(detail::icons::Logo.c_str());
    if (ImGui::BeginMenu("Edit"))
    {
        const Command* const nextUndo = state.history.nextUndo();
        const std::string undoLabel = nextUndo != nullptr ? std::format("Undo {}", nextUndo->description()) : "Undo";
        if (ImGui::MenuItemEx(undoLabel.c_str(), detail::icons::Undo.c_str(), "Ctrl+Z", false, nextUndo != nullptr))
        {
            undo(state, scene);
        }
        const Command* const nextRedo = state.history.nextRedo();
        const std::string redoLabel = nextRedo != nullptr ? std::format("Redo {}", nextRedo->description()) : "Redo";
        if (ImGui::MenuItemEx(redoLabel.c_str(), detail::icons::Redo.c_str(), "Ctrl+Y", false, nextRedo != nullptr))
        {
            redo(state, scene);
        }
        ImGui::Separator();
        if (ImGui::BeginMenuEx("Create", detail::icons::Plus.c_str()))
        {
            detail::drawCreateEntityMenu(state, core::Uuid{});
            ImGui::EndMenu();
        }
        ImGui::Separator();
        detail::drawEntityEditMenuItems(state, scene);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem(detail::hierarchyWindow, nullptr, &state.showHierarchy);
        ImGui::MenuItem(detail::inspectorWindow, nullptr, &state.showInspector);
        ImGui::MenuItem(detail::assetsWindow, nullptr, &state.showAssets);
        ImGui::MenuItem(detail::animationWindow, nullptr, &state.showAnimation);
        ImGui::MenuItem(detail::consoleWindow, nullptr, &state.showConsole);
        ImGui::MenuItem(detail::statisticsWindow, nullptr, &state.showStatistics);
        ImGui::MenuItem(detail::profilerWindow, nullptr, &state.showProfiler);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout"))
        {
            state.resetLayout = true;
        }
        ImGui::EndMenu();
    }

    const char* const hint = "F1 hides the tools";
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(hint).x - ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::TextDisabled("%s", hint);
    ImGui::EndMainMenuBar();
}

// Scene tree and file system on the left, inspector on the right, output and statistics under the view.
void buildDefaultLayout(ImGuiID dockspace, const ImGuiViewport& viewport, ToolsMode mode)
{
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, viewport.WorkSize);

    ImGuiID center = dockspace;
    ImGuiID leftTop = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.19f, nullptr, &center);
    const ImGuiID leftBottom = ImGui::DockBuilderSplitNode(leftTop, ImGuiDir_Down, 0.5f, nullptr, &leftTop);
    const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.27f, nullptr, &center);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.26f, nullptr, &center);

    ImGui::DockBuilderDockWindow(detail::hierarchyWindow, leftTop);
    ImGui::DockBuilderDockWindow(detail::assetsWindow, leftBottom);
    ImGui::DockBuilderDockWindow(detail::inspectorWindow, right);
    ImGui::DockBuilderDockWindow(detail::consoleWindow, bottom);
    ImGui::DockBuilderDockWindow(detail::statisticsWindow, bottom);
    ImGui::DockBuilderDockWindow(detail::profilerWindow, bottom);
    ImGui::DockBuilderDockWindow(detail::animationWindow, bottom);
    if (mode == ToolsMode::Editor)
    {
        // The screens of the menu bar share the middle: the switch brings one to the front, and
        // the node shows no tab bar, so that the screen fills it as in Godot.
        ImGui::DockBuilderDockWindow(detail::viewportWindow, center);
        ImGui::DockBuilderDockWindow(detail::textEditorWindow, center);
        ImGui::DockBuilderDockWindow(detail::interfaceWindow, center);
        if (ImGuiDockNode* const node = ImGui::DockBuilderGetNode(center))
        {
            node->SetLocalFlags(node->LocalFlags | ImGuiDockNodeFlags_NoTabBar |
                                ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoCloseButton);
        }
    }
    else
    {
        ImGui::DockBuilderDockWindow(detail::textEditorWindow, bottom);
    }
    ImGui::DockBuilderFinish(dockspace);
}

void drawDockspace(ToolsState& state)
{
    const ImGuiViewport* const viewport = ImGui::GetMainViewport();
    // The name carries a version, increased when panels change, so that saved layouts from before
    // are rebuilt with the new panels docked.
    const bool editor = state.mode == ToolsMode::Editor;
    const ImGuiID dockspace = ImHashStr(editor ? "Devex editor dockspace 6" : "Devex tools dockspace 6");
    if (state.resetLayout || ImGui::DockBuilderGetNode(dockspace) == nullptr)
    {
        buildDefaultLayout(dockspace, *viewport, state.mode);
        state.resetLayout = false;
        state.selectOutputTabFrames = 2;
    }
    // Over the game, the central node stays empty and transparent, showing the game behind the
    // panels; the editor shows the game in its viewport panel instead. The gaps between panels
    // show the outer color, as in Godot.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, detail::uiColor(detail::themeColors().outer));
    ImGui::DockSpaceOverViewport(dockspace, viewport, editor ? ImGuiDockNodeFlags_None : ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::PopStyleColor();
}

void finishFrame(ToolsState& state)
{
    ImGui::Render();
    state.renderer.queueImGuiDrawData();
}

void handleShortcuts(ToolsState& state, scene::Scene& scene)
{
    if (detail::textEditorFocused())
    {
        return;
    }
    // Text fields route these chords to their own undo first.
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal))
    {
        undo(state, scene);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal) ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal))
    {
        redo(state, scene);
    }
}

void updateEditor(ToolsState& state, scene::Scene& scene, PlayState playState)
{
    // Edits made while playing apply to the played copy and have their own history, dropped when
    // play stops.
    if ((state.playState == PlayState::Editing) != (playState == PlayState::Editing))
    {
        std::swap(state.history, state.suspendedHistory);
        // Starting to play sets the edit history aside; stopping brings it back and forgets the
        // edits of the session.
        (playState == PlayState::Editing ? state.suspendedHistory : state.history).clear();
        state.gizmo.end();
        state.clickStart.reset();
        state.awaitedPick = 0;
        if (state.flying)
        {
            state.flying = false;
            state.window.setMouseCaptured(false);
        }
        state.orbiting = false;
        state.panning = false;
        // The game receives the keyboard right away.
        state.focusViewport = playState != PlayState::Editing;
    }
    state.playState = playState;

    detail::updateEditorSession(state, scene);
    if (state.database == nullptr)
    {
        applyWindowLayout(state, detail::WindowLayout::ProjectManager);
        detail::drawProjectManager(state);
        detail::drawSettingsWindow(state);
        state.viewportPixels = {};
        detail::updateWindowTitle(state, scene);
        finishFrame(state);
        const ImGuiIO& io = ImGui::GetIO();
        state.capturesKeyboard = io.WantCaptureKeyboard;
        state.capturesMouse = io.WantCaptureMouse;
        return;
    }

    applyWindowLayout(state, detail::WindowLayout::Editor);
    detail::drawEditorMenus(state, scene);
    if (std::exchange(state.mainScreenChanged, false))
    {
        ImGui::SetWindowFocus(detail::windowOf(state.mainScreen));
    }
    detail::drawStatusBar(state, scene);
    drawDockspace(state);
    handleShortcuts(state, scene);
    detail::handleEditorShortcuts(state, scene);
    detail::drawViewportPanel(state, scene);
    if (state.showInterface)
    {
        detail::drawInterfacePanel(state, scene);
    }
    if (state.showHierarchy)
    {
        detail::drawHierarchyPanel(state, scene);
    }
    if (state.showInspector)
    {
        detail::drawInspectorPanel(state, scene);
    }
    if (state.showStatistics)
    {
        detail::drawStatisticsPanel(state, scene);
    }
    detail::drawProfilerPanel(state);
    if (state.showConsole)
    {
        detail::drawConsolePanel(state);
    }
    if (state.showAssets)
    {
        detail::drawAssetsPanel(state, scene);
    }
    detail::drawSettingsWindow(state);
    detail::drawProjectSettingsWindow(state);
    detail::drawExportWindow(state);
    detail::drawDebuggingWindow(state);
    detail::drawNewScriptPopup(state);
    detail::updatePendingScript(state, scene);
    detail::drawTextEditorPanel(state, scene);
    detail::drawAnimationPanel(state, scene);
    detail::drawEditorPopups(state, scene);
    detail::handleEntityShortcuts(state, scene);
    if (state.pendingCommand != nullptr)
    {
        logFailure(state.history.execute(scene, std::exchange(state.pendingCommand, nullptr)));
    }
    detail::updateWindowTitle(state, scene);
    finishFrame(state);

    // Gameplay receives the devices the game view uses, and the editor camera flies with them.
    const ImGuiIO& io = ImGui::GetIO();
    const bool playing = playState != PlayState::Editing;
    state.capturesKeyboard = io.WantCaptureKeyboard && !state.flying && !(playing && state.viewportFocused);
    state.capturesMouse = io.WantCaptureMouse && !state.flying && !(playing && state.viewportHovered);
}

// The Devex logo as the icon of the window and its taskbar button.
void setWindowIcon(ToolsState& state)
{
    constexpr std::uint32_t size = 64;
    const core::Result<detail::SvgImage> image = detail::renderSvg(state.icons.svg(detail::Icon::Logo), size, size);
    if (image)
    {
        state.window.setIcon(image->rgba, image->width, image->height);
    }
}

} // namespace

core::Result<std::unique_ptr<ToolsOverlay>> ToolsOverlay::create(
    platform::Platform& platform, platform::Window& window, render::Renderer& renderer,
    const std::filesystem::path& settingsFile, ToolsMode mode, const std::filesystem::path& userSettingsFile)
{
    DEVEX_ASSERT_MSG(ImGui::GetCurrentContext() == nullptr, "only one ToolsOverlay may exist");
    IMGUI_CHECKVERSION();

    auto state = std::make_unique<detail::ToolsState>(platform, window, renderer, mode);
    state->settingsFile = core::toUtf8(settingsFile);
    const std::filesystem::path resources = platform.baseDirectory() / "resources";
    state->icons = detail::IconSet::load(resources / "icons");
    if (mode == ToolsMode::Editor)
    {
        state->visible = true;
        detail::loadUserSettings(*state, userSettingsFile);
        setWindowIcon(*state);
    }

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = state->settingsFile.c_str();
    // Ctrl+Tab goes through scene tabs rather than ImGui's windows.
    ImGui::GetCurrentContext()->ConfigNavWindowingKeyNext = 0;
    ImGui::GetCurrentContext()->ConfigNavWindowingKeyPrev = 0;
    state->fonts = detail::loadEditorFonts(resources / "fonts", state->icons);
    detail::setEditorFonts(state->fonts);

    if (core::Result<void> connected = platform.initializeImGui(window); !connected)
    {
        ImGui::DestroyContext();
        return std::unexpected(connected.error());
    }
    if (core::Result<void> connected = renderer.initializeImGui(); !connected)
    {
        platform.shutdownImGui();
        ImGui::DestroyContext();
        return std::unexpected(connected.error());
    }
    refreshTheme(*state);
    return std::unique_ptr<ToolsOverlay>(new ToolsOverlay(std::move(state)));
}

ToolsOverlay::ToolsOverlay(std::unique_ptr<detail::ToolsState> state) noexcept
    : m_state(std::move(state))
{
}

ToolsOverlay::~ToolsOverlay()
{
    if (m_state->mode == ToolsMode::Editor)
    {
        detail::saveEditorSettings(*m_state);
        if (m_state->flying)
        {
            m_state->window.setMouseCaptured(false);
        }
    }
    m_state->renderer.shutdownImGui();
    m_state->platform.shutdownImGui();
    ImGui::DestroyContext();
}

ToolsMode ToolsOverlay::mode() const noexcept
{
    return m_state->mode;
}

bool ToolsOverlay::isVisible() const noexcept
{
    return m_state->visible;
}

void ToolsOverlay::setVisible(bool visible) noexcept
{
    if (m_state->mode == ToolsMode::Editor)
    {
        return;
    }
    m_state->visible = visible;
    if (!visible)
    {
        m_state->capturesKeyboard = false;
        m_state->capturesMouse = false;
    }
}

bool ToolsOverlay::capturesKeyboard() const noexcept
{
    return m_state->capturesKeyboard;
}

bool ToolsOverlay::capturesMouse() const noexcept
{
    return m_state->capturesMouse;
}

math::Extent2D ToolsOverlay::viewportPixels() const noexcept
{
    return m_state->viewportPixels;
}

std::optional<math::Vec2> ToolsOverlay::viewportPointer() const noexcept
{
    if (!m_state->viewportHovered || m_state->viewportPixels.width == 0)
    {
        return std::nullopt;
    }
    const ImGuiIO& io = ImGui::GetIO();
    return (math::Vec2(io.MousePos.x, io.MousePos.y) - m_state->viewportOrigin) *
           m_state->pixelsPerPoint;
}

void ToolsOverlay::update(scene::Scene& scene, core::Duration frameDelta, PlayState playState)
{
    ToolsState& state = *m_state;
    state.frameTimes.record(static_cast<float>(frameDelta.count() * 1000.0));
    // Frames are measured only for someone to look at them.
    core::profiler::setEnabled(state.visible && state.showProfiler);
    if (!state.visible)
    {
        return;
    }

    refreshTheme(state);
    state.platform.beginImGuiFrame();
    state.renderer.beginImGuiFrame();
    ImGui::NewFrame();

    if (state.mode == ToolsMode::Editor)
    {
        updateEditor(state, scene, playState);
        return;
    }

    drawOverlayMenu(state, scene);
    drawDockspace(state);
    handleShortcuts(state, scene);
    if (state.showHierarchy)
    {
        detail::drawHierarchyPanel(state, scene);
    }
    if (state.showInspector)
    {
        detail::drawInspectorPanel(state, scene);
    }
    if (state.showStatistics)
    {
        detail::drawStatisticsPanel(state, scene);
    }
    detail::drawProfilerPanel(state);
    if (state.showConsole)
    {
        detail::drawConsolePanel(state);
    }
    if (state.showAssets)
    {
        detail::drawAssetsPanel(state, scene);
    }
    detail::handleEntityShortcuts(state, scene);
    if (state.pendingCommand != nullptr)
    {
        logFailure(state.history.execute(scene, std::exchange(state.pendingCommand, nullptr)));
    }

    ImGui::Render();
    state.renderer.queueImGuiDrawData();

    const ImGuiIO& io = ImGui::GetIO();
    state.capturesKeyboard = io.WantCaptureKeyboard;
    state.capturesMouse = io.WantCaptureMouse;
}

void ToolsOverlay::prepareRender(scene::Scene& scene, render::RenderWorld& world, PlayState playState)
{
    ToolsState& state = *m_state;
    if (state.mode != ToolsMode::Editor)
    {
        return;
    }
    // A hidden viewport still renders, at a size too small to cost anything.
    world.viewport = state.viewportPixels.width > 0 ? state.viewportPixels : math::Extent2D{16, 16};
    if (playState == PlayState::Editing)
    {
        world.camera.view = state.camera.view();
        world.camera.verticalFov = detail::EditorCamera::verticalFov;
        world.camera.nearPlane = detail::EditorCamera::nearPlane;
        if (state.database != nullptr)
        {
            detail::addEditorOverlay(state, scene, world);
        }
    }
}

EditorRequests ToolsOverlay::takeRequests() noexcept
{
    EditorRequests requests = std::exchange(m_state->requests, EditorRequests{});
    requests.waitForDebugger = requests.play && m_state->waitForDebugger && m_state->debugger.available;
    return requests;
}

void ToolsOverlay::setGameCodeStatus(GameCodeStatus status)
{
    m_state->gameCode = std::move(status);
}

void ToolsOverlay::setProjectCodeStatusProvider(std::function<ProjectCodeStatus(const asset::Project&)> provider)
{
    m_state->projectCodeStatus = std::move(provider);
    m_state->projectManager.refresh = true;
}

void ToolsOverlay::setDebuggerStatus(DebuggerStatus status)
{
    if (status.waiting && !m_state->debugger.waiting)
    {
        // Waiting shows how to attach.
        m_state->showDebugging = true;
    }
    m_state->debugger = status;
}

void ToolsOverlay::setEngineBuilds(std::vector<EngineBuildChoice> builds)
{
    m_state->engineBuilds = std::move(builds);
}

void ToolsOverlay::setExportStatus(ExportStatus status)
{
    m_state->exportStatus = std::move(status);
}

bool ToolsOverlay::confirmClose(scene::Scene& editedScene)
{
    ToolsState& state = *m_state;
    if (state.mode != ToolsMode::Editor || state.database == nullptr)
    {
        return true;
    }
    if (state.playState == PlayState::Editing && !detail::hasUnsavedChanges(state, editedScene))
    {
        return true;
    }
    detail::requestAction(state, editedScene, {.kind = detail::PendingAction::Kind::Quit});
    return false;
}

void ToolsOverlay::forEachBackgroundScene(const std::function<void(scene::Scene&)>& function)
{
    m_state->tabs.forEachBackgroundScene(function);
}

void ToolsOverlay::setAssetDatabase(asset::AssetDatabase* database) noexcept
{
    ToolsState& state = *m_state;
    if (state.database == database)
    {
        return;
    }
    if (state.mode == ToolsMode::Editor && state.database != nullptr)
    {
        detail::saveEditorSettings(state);
    }
    // The scenes of the previous project go with it; the application empties the edited scene.
    scene::Scene dropped;
    state.tabs.clear(detail::ActiveDocument{state.scenePath, dropped, state.history, state.savedState, state.selection,
                                            state.camera, state.hiddenEntities});
    state.pendingAction.reset();
    state.textDocuments.clear();
    state.activeText.clear();
    state.textOpenError.clear();
    state.showTextEditor = false;
    state.selectedCode.clear();
    state.dialogAnswers->openText.reset();
    state.resumeActionAfterPlay = false;
    detail::stopAudioPreview(state);
    state.selectedAsset = {};
    state.selectedClipInfo.reset();
    state.database = database;
    state.projectChanged = database != nullptr;
    state.projectManager.refresh = true;
}

void ToolsOverlay::openTextFile(const std::filesystem::path& file)
{
    detail::openTextFile(*m_state, file);
}

void ToolsOverlay::setAudio(audio::AudioEngine* engine,
                            std::function<std::shared_ptr<const audio::Clip>(asset::AssetId)> clips)
{
    detail::stopAudioPreview(*m_state);
    m_state->audio = engine;
    m_state->audioClips = std::move(clips);
}

void ToolsOverlay::setCodeDiagnostics(std::vector<CodeDiagnostic> diagnostics)
{
    m_state->codeDiagnostics = std::move(diagnostics);
}

void ToolsOverlay::setAnimationClips(std::function<std::shared_ptr<const animation::Clip>(asset::AssetId)> clips)
{
    m_state->animationClips = std::move(clips);
}

void ToolsOverlay::setThemes(std::function<std::shared_ptr<const asset::ThemeData>(asset::AssetId)> themes)
{
    m_state->themes = std::move(themes);
}

void ToolsOverlay::setMemoryReport(std::function<asset::MemoryReport()> report)
{
    m_state->memoryReport = std::move(report);
}

void ToolsOverlay::setPendingLoads(std::function<std::size_t()> pending)
{
    m_state->pendingLoads = std::move(pending);
}

void ToolsOverlay::setAnimationWorld(animation::AnimationWorld* world) noexcept
{
    m_state->animationWorld = world;
    m_state->animationPreviewPlaying = false;
}

CommandHistory& ToolsOverlay::history() noexcept
{
    return m_state->history;
}

} // namespace devex::tools
