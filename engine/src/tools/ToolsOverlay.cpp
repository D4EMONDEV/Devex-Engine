#include "EditorFrame.hpp"
#include "EditorModal.hpp"
#include "EditorUi.hpp"
#include "SettingsUi.hpp"
#include "ToolsState.hpp"
#include "TwoDScreen.hpp"

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

std::optional<asset::AssetId> acceptDroppedAsset(ToolsState& state, std::optional<asset::AssetType> type)
{
    // What a panel carries, let go over the last image shown: the view.
    EditorUiKit& kit = editorUiKit(state);
    const EditorDrag* const carried = kit.carried();
    if (carried == nullptr || !carried->is(assetPayload, sizeof(AssetPayload)) || !state.hosts.itemHovered() ||
        !state.input.released(Mouse::Left))
    {
        return std::nullopt;
    }
    AssetPayload payload;
    std::memcpy(&payload, carried->payload.data(), sizeof(payload));
    if (type && payload.type != *type)
    {
        return std::nullopt;
    }
    kit.forgetCarried();
    return asset::AssetId{uuidFromBytes(payload.uuid)};
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

// Where the strips of the frame stand, and the room the dock or the project manager has between them.
void layoutScreen(ToolsState& state)
{
    const ImGuiViewport* const viewport = ImGui::GetMainViewport();
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 min = viewport->Pos;
    const ImVec2 max(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
    const bool framed = state.mode != ToolsMode::Editor || state.database != nullptr;
    const float menu = framed ? std::round(ImGui::GetFontSize() + style.FramePadding.y * 3.2f) : 0.0f;
    const float status = state.mode == ToolsMode::Editor && framed ? std::round(ImGui::GetFrameHeight() + style.FramePadding.y * 1.2f) : 0.0f;
    state.menuBarMin = min;
    state.menuBarMax = ImVec2(max.x, min.y + menu);
    state.statusBarMin = ImVec2(min.x, max.y - status);
    state.statusBarMax = max;
    state.workMin = ImVec2(min.x, min.y + menu);
    state.workMax = ImVec2(max.x, max.y - status);
}

void finishFrame(ToolsState& state)
{
    // The images of the frame, from the dock up; the shape of the pointer; and typing, on while a
    // field takes it, with the input method of the system next to the field.
    state.hosts.compose(*ImGui::GetBackgroundDrawList());
    state.platform.setCursor(state.input.cursor);
    const std::optional<std::pair<ImVec2, ImVec2>> typing = editorUiKit(state).takeTextInput();
    state.typing = typing.has_value();
    if (typing)
    {
        state.platform.setTextInputArea(state.window, math::Vec2{typing->first.x, typing->first.y},
                                        math::Vec2{typing->second.x, typing->second.y});
    }
    state.platform.setTextInput(state.window, state.typing);
    ImGui::Render();
    state.renderer.queueImGuiDrawData();
}

void handleShortcuts(ToolsState& state, scene::Scene& scene)
{
    // The text editor and the graph of the Animator panel undo their own changes; a modal holds
    // the editor.
    if (detail::textEditorFocused(state) || state.animatorEditor.focused || detail::isModalOpen(state))
    {
        return;
    }
    // A field being typed into undoes its own letters.
    if (state.typing)
    {
        return;
    }
    using detail::KeyModifiers;
    if (state.input.chord(KeyModifiers{.ctrl = true}, 'z'))
    {
        undo(state, scene);
    }
    if (state.input.chord(KeyModifiers{.ctrl = true}, 'y') || state.input.chord(KeyModifiers{.ctrl = true, .shift = true}, 'z'))
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
        state.capturesKeyboard = true;
        state.capturesMouse = state.hosts.pointerTaken();
        return;
    }

    applyWindowLayout(state, detail::WindowLayout::Editor);
    // The screen follows the kind of the scene when it changes, from the Scene menu or its undo.
    if (state.shownSceneKind && *state.shownSceneKind != scene.kind() && state.mainScreen != detail::MainScreen::Script &&
        state.playState == PlayState::Editing)
    {
        detail::showSceneScreen(state, scene);
    }
    state.shownSceneKind = scene.kind();
    detail::drawEditorMenus(state, scene);
    if (std::exchange(state.mainScreenChanged, false))
    {
        detail::focusPanel(state, detail::windowOf(state.mainScreen));
    }
    detail::drawStatusBar(state, scene);
    detail::drawEditorDock(state);
    handleShortcuts(state, scene);
    detail::handleEditorShortcuts(state, scene);
    detail::drawViewportPanel(state, scene);
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
    detail::drawCreationDialog(state, scene);
    detail::drawExportWindow(state);
    detail::drawDebuggingWindow(state);
    detail::updatePendingScript(state, scene);
    detail::drawTextEditorPanel(state, scene);
    detail::drawAnimationPanel(state, scene);
    detail::drawAnimatorPanel(state, scene);
    detail::drawEditorPopups(state, scene);
    detail::handleEntityShortcuts(state, scene);
    // Over every window: the menus of the menu bar, and the tooltips of the strips.
    detail::drawEditorLayer(state, scene);
    if (state.pendingCommand != nullptr)
    {
        logFailure(state.history.execute(scene, std::exchange(state.pendingCommand, nullptr)));
    }
    detail::updateWindowTitle(state, scene);
    finishFrame(state);

    // Gameplay receives the devices the game view uses, and the editor camera flies with them.
    const bool playing = playState != PlayState::Editing;
    state.capturesKeyboard = !state.hosts.focusedId().empty() && !state.flying && !(playing && state.viewportFocused);
    state.capturesMouse = state.hosts.pointerTaken() && !state.flying && !(playing && state.viewportHovered);
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
    // The editor places its images, reads the devices and shapes the pointer itself: ImGui only
    // draws the images where the editor puts them.
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.IniFilename = state->settingsFile.c_str();
    // Ctrl+Tab goes through scene tabs rather than ImGui's windows.
    ImGui::GetCurrentContext()->ConfigNavWindowingKeyNext = 0;
    ImGui::GetCurrentContext()->ConfigNavWindowingKeyPrev = 0;
    state->fonts = detail::loadEditorFonts(resources / "fonts", state->icons);

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

bool ToolsOverlay::wantsTextInput() const noexcept
{
    return m_state->visible && m_state->typing;
}

math::Extent2D ToolsOverlay::viewportPixels() const noexcept
{
    return m_state->viewportPixels;
}

std::optional<InterfaceFrame> ToolsOverlay::interfaceFrame() const noexcept
{
    return m_state->playState == PlayState::Editing && m_state->showViewport ? m_state->interfaceFrame : std::nullopt;
}

std::optional<math::Vec2> ToolsOverlay::viewportPointer() const noexcept
{
    if (!m_state->viewportHovered || m_state->viewportPixels.width == 0)
    {
        return std::nullopt;
    }
    return (m_state->input.mouse() - m_state->viewportOrigin) * m_state->pixelsPerPoint;
}

void ToolsOverlay::update(scene::Scene& scene, core::Duration frameDelta, PlayState playState)
{
    ToolsState& state = *m_state;
    state.pressedKey = std::exchange(state.notifiedKey, std::nullopt);
    state.frameTimes.record(static_cast<float>(frameDelta.count() * 1000.0));
    detail::recordMonitors(state, scene, frameDelta);
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
    state.clock += frameDelta.count();
    state.input.begin(state.platform.toolsInput(), state.clock, static_cast<float>(frameDelta.count()));
    // Where the strips and the panels stand, which host the pointer is over, and what the panels read.
    layoutScreen(state);
    using detail::Mouse;
    state.hosts.beginFrame(state.input.mouse(),
                           state.input.clicked(Mouse::Left) || state.input.clicked(Mouse::Right) || state.input.clicked(Mouse::Middle));
    detail::editorUiKit(state).setFrame(state.hosts, state.input, state.platform);
    detail::pruneModals(state);

    if (state.mode == ToolsMode::Editor)
    {
        updateEditor(state, scene, playState);
        return;
    }

    // The menu bar of the editor, with the menus the tools over a game have.
    detail::drawEditorMenus(state, scene);
    detail::drawEditorDock(state);
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
    detail::drawCreationDialog(state, scene);
    detail::handleEntityShortcuts(state, scene);
    detail::drawEditorLayer(state, scene);
    if (state.pendingCommand != nullptr)
    {
        logFailure(state.history.execute(scene, std::exchange(state.pendingCommand, nullptr)));
    }

    finishFrame(state);
    state.capturesKeyboard = !state.hosts.focusedId().empty();
    state.capturesMouse = state.hosts.pointerTaken();
}

void ToolsOverlay::prepareRender(scene::Scene& scene, render::RenderWorld& world, PlayState playState)
{
    ToolsState& state = *m_state;
    // The panels made with the interface of the engine draw into images of their own, in the
    // overlay of a game as in the editor.
    detail::renderProjectManager(state, world);
    detail::renderEditorDock(state, world);
    detail::renderModalLayer(state, world);
    detail::renderFileSystem(state, world);
    detail::renderOutput(state, world);
    detail::renderSceneTree(state, world);
    detail::renderInspector(state, world);
    detail::renderCreationDialog(state, world);
    detail::renderFormWindows(state, world);
    detail::renderStatistics(state, world);
    detail::renderProfiler(state, world);
    detail::renderAnimationPanel(state, world);
    detail::renderAnimatorPanel(state, world);
    detail::renderTextEditor(state, world);
    detail::renderViewportOverlay(state, world);
    detail::renderEditorFrame(state, world);
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
        // The 2D screen looks straight at the XY plane, whatever the cameras of the scene do.
        const bool twoD = state.camera.isTwoD();
        world.camera.projection = twoD ? render::Projection::Orthographic : render::Projection::Perspective;
        world.camera.orthographicSize = state.camera.orthographicSize();
        world.camera.farPlane = detail::EditorCamera::twoDFarPlane;
        state.gizmo.twoD = twoD;
        // The screen of the other kind shows nothing of the scene, as the 3D view of Godot shows
        // nothing of a 2D scene: a neutral sky and the grid, and the interfaces of a 3D scene in 2D.
        if (detail::screenContent(twoD, scene) != detail::ScreenContent::Scene)
        {
            world.sun = render::RenderSun{};
            world.lights.clear();
            world.environment = render::RenderEnvironment{};
            // The same gray whatever the camera of the scene exposes for, as a 2D game's does for its
            // sprites.
            world.camera.autoExposure = false;
            world.camera.ev100 = 14.0f;
            world.camera.exposureCompensation = 0.0f;
            world.camera.tonemapper = render::Tonemapper::AgX;
            world.meshes.clear();
            world.boneMatrices.clear();
            world.particles.clear();
            world.trailPoints.clear();
            world.trailSegments.clear();
            world.particleDraws.clear();
            world.sprites.clear();
            world.tilemaps.clear();
            world.tiles.clear();
            world.sceneLines.clear();
        }
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

void ToolsOverlay::select(std::span<const core::Uuid> entities)
{
    m_state->selection.set(entities);
}

void ToolsOverlay::selectAsset(asset::AssetId asset)
{
    detail::selectAsset(*m_state, asset);
}

void ToolsOverlay::openWindow(EditorWindow window)
{
    switch (window)
    {
    case EditorWindow::EditorSettings:
        m_state->showSettings = true;
        break;
    case EditorWindow::ProjectSettings:
        m_state->showProjectSettings = true;
        break;
    case EditorWindow::Export:
        m_state->showExport = true;
        break;
    case EditorWindow::Debugging:
        m_state->showDebugging = true;
        break;
    case EditorWindow::NewScript:
        m_state->openNewScriptPopup = true;
        break;
    case EditorWindow::About:
        m_state->openAboutPopup = true;
        break;
    case EditorWindow::Profiler:
        m_state->showProfiler = true;
        m_state->focusProfiler = true;
        break;
    case EditorWindow::Animation:
        m_state->showAnimation = true;
        m_state->focusAnimation = true;
        break;
    case EditorWindow::Animator:
        m_state->showAnimator = true;
        m_state->focusAnimator = true;
        break;
    }
}

void ToolsOverlay::openMenu(EditorMenu menu)
{
    m_state->menuRequest = static_cast<std::size_t>(menu);
}

void ToolsOverlay::closeMenu()
{
    m_state->menuRequest.reset();
    detail::closeEditorMenu(*m_state);
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

void ToolsOverlay::setSpriteSources(std::function<render::TextureHandle(asset::AssetId)> textures,
                                    std::function<math::Extent2D(asset::AssetId)> textureSizes,
                                    std::function<std::shared_ptr<const asset::SpriteData>(asset::AssetId)> sprites,
                                    std::function<std::shared_ptr<const asset::TilesetData>(asset::AssetId)> tilesets)
{
    m_state->textures = std::move(textures);
    m_state->textureSizes = std::move(textureSizes);
    m_state->sprites = std::move(sprites);
    m_state->tilesets = std::move(tilesets);
}

void ToolsOverlay::setNavigationSources(std::function<const asset::MeshData*(asset::AssetId)> meshes,
                                        std::function<std::shared_ptr<const asset::NavMeshData>(asset::AssetId)> navMeshes)
{
    m_state->meshes = std::move(meshes);
    m_state->navMeshes = std::move(navMeshes);
}

void ToolsOverlay::setMemoryReport(std::function<asset::MemoryReport()> report)
{
    m_state->memoryReport = std::move(report);
}

void ToolsOverlay::setPendingLoads(std::function<std::size_t()> pending)
{
    m_state->pendingLoads = std::move(pending);
}

std::filesystem::path ToolsOverlay::scenePath() const
{
    return m_state->scenePath;
}

void ToolsOverlay::notifyKeyPressed(platform::Key key) noexcept
{
    m_state->notifiedKey = key;
}

void ToolsOverlay::setNavigationWorld(navigation::NavigationWorld* world) noexcept
{
    m_state->navigationWorld = world;
}

void ToolsOverlay::setAnimationWorld(animation::AnimationWorld* world) noexcept
{
    m_state->animationWorld = world;
    m_state->animationPreviewPlaying = false;
}

void ToolsOverlay::setParticleWorld(particles::ParticleWorld* world) noexcept
{
    m_state->particleWorld = world;
}

CommandHistory& ToolsOverlay::history() noexcept
{
    return m_state->history;
}

} // namespace devex::tools
