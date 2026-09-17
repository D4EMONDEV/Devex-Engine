#include "ToolsState.hpp"

#include <devex/asset/Artifact.hpp>
#include <devex/core/Assert.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/ModelInstantiation.hpp>
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
        if (peeked != nullptr && peeked->IsDataType(assetPayload) &&
            peeked->DataSize == sizeof(AssetPayload))
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
    const asset::AssetInfo* const info =
        state.database != nullptr ? state.database->find(model) : nullptr;
    if (info == nullptr || info->type != asset::AssetType::Model)
    {
        return;
    }
    const core::Result<std::vector<std::byte>> bytes = state.database->loadArtifact(model);
    const core::Result<asset::ModelData> data =
        bytes ? asset::decodeModel(*bytes)
              : core::Result<asset::ModelData>(std::unexpected(bytes.error()));
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
    state.pendingCommand = makeCreateEntityTreeCommand(scene::saveEntityTree(scratch, root), rootUuid,
                                                       parent, std::format("Place {}", info->name));
    state.selection = rootUuid;
}

void requestCreateEntity(ToolsState& state, core::Uuid parent)
{
    const core::Uuid entity = core::Uuid::generate();
    state.pendingCommand = makeCreateEntityCommand(entity, "Entity", parent);
    state.selection = entity;
}

ImVec4 linearColor(ImVec4 srgb) noexcept
{
    const auto toLinear = [](float channel) {
        return channel <= 0.04045f ? channel / 12.92f
                                   : std::pow((channel + 0.055f) / 1.055f, 2.4f);
    };
    return {toLinear(srgb.x), toLinear(srgb.y), toLinear(srgb.z), srgb.w};
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

void applyStyle()
{
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.TabRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.WindowBorderSize = 1.0f;
    style.Colors[ImGuiCol_WindowBg].w = 0.94f;

    // ImGui colors are authored in sRGB, but the swapchain encodes linear values.
    for (ImVec4& color : style.Colors)
    {
        color = detail::linearColor(color);
    }
}

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

void drawMainMenu(ToolsState& state, scene::Scene& scene)
{
    if (!ImGui::BeginMainMenuBar())
    {
        return;
    }

    if (ImGui::BeginMenu("Edit"))
    {
        const Command* const nextUndo = state.history.nextUndo();
        const std::string undoLabel =
            nextUndo != nullptr ? std::format("Undo {}", nextUndo->description()) : "Undo";
        if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, nextUndo != nullptr))
        {
            undo(state, scene);
        }
        const Command* const nextRedo = state.history.nextRedo();
        const std::string redoLabel =
            nextRedo != nullptr ? std::format("Redo {}", nextRedo->description()) : "Redo";
        if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, nextRedo != nullptr))
        {
            redo(state, scene);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Entity"))
    {
        const bool hasSelection = scene.findEntity(state.selection).isValid();
        if (ImGui::MenuItem("Create entity"))
        {
            detail::requestCreateEntity(state, core::Uuid{});
        }
        if (ImGui::MenuItem("Create child", nullptr, false, hasSelection))
        {
            detail::requestCreateEntity(state, state.selection);
        }
        if (ImGui::MenuItem("Delete", "Delete", false, hasSelection))
        {
            state.pendingCommand = makeDestroyEntityCommand(state.selection);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem(detail::hierarchyWindow, nullptr, &state.showHierarchy);
        ImGui::MenuItem(detail::inspectorWindow, nullptr, &state.showInspector);
        ImGui::MenuItem(detail::statisticsWindow, nullptr, &state.showStatistics);
        ImGui::MenuItem(detail::consoleWindow, nullptr, &state.showConsole);
        ImGui::MenuItem(detail::assetsWindow, nullptr, &state.showAssets);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset layout"))
        {
            state.resetLayout = true;
        }
        ImGui::EndMenu();
    }

    const char* const hint = "F1 hides the tools";
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(hint).x -
                    ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::TextDisabled("%s", hint);
    ImGui::EndMainMenuBar();
}

void buildDefaultLayout(ImGuiID dockspace, const ImGuiViewport& viewport, ToolsMode mode)
{
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, viewport.WorkSize);

    ImGuiID center = dockspace;
    const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.2f, nullptr, &center);
    const ImGuiID right =
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.3f, nullptr, &center);
    const ImGuiID bottom =
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, nullptr, &center);

    ImGui::DockBuilderDockWindow(detail::hierarchyWindow, left);
    ImGui::DockBuilderDockWindow(detail::inspectorWindow, right);
    ImGui::DockBuilderDockWindow(detail::assetsWindow, bottom);
    ImGui::DockBuilderDockWindow(detail::consoleWindow, bottom);
    ImGui::DockBuilderDockWindow(detail::statisticsWindow, bottom);
    if (mode == ToolsMode::Editor)
    {
        ImGui::DockBuilderDockWindow(detail::viewportWindow, center);
    }
    ImGui::DockBuilderFinish(dockspace);
}

void drawDockspace(ToolsState& state)
{
    const ImGuiViewport* const viewport = ImGui::GetMainViewport();
    // The name carries a version, increased when panels are added, so that saved layouts from
    // before are rebuilt with the new panels docked.
    const bool editor = state.mode == ToolsMode::Editor;
    const ImGuiID dockspace = ImHashStr(editor ? "Devex editor dockspace 1" : "Devex tools dockspace 2");
    if (state.resetLayout || ImGui::DockBuilderGetNode(dockspace) == nullptr)
    {
        buildDefaultLayout(dockspace, *viewport, state.mode);
        state.resetLayout = false;
    }
    // Over the game, the central node stays empty and transparent, showing the game behind the
    // panels; the editor shows the game in its viewport panel instead.
    ImGui::DockSpaceOverViewport(dockspace, viewport, editor ? ImGuiDockNodeFlags_None : ImGuiDockNodeFlags_PassthruCentralNode);
}

void finishFrame(ToolsState& state)
{
    ImGui::Render();
    state.renderer.queueImGuiDrawData();
}

void handleShortcuts(ToolsState& state, scene::Scene& scene);

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
    }
    state.playState = playState;

    detail::updateEditorSession(state, scene);
    if (state.database == nullptr)
    {
        detail::drawWelcomeScreen(state);
        detail::drawEditorPopups(state, scene);
        state.viewportPixels = {};
        detail::updateWindowTitle(state, scene);
        finishFrame(state);
        const ImGuiIO& io = ImGui::GetIO();
        state.capturesKeyboard = io.WantCaptureKeyboard;
        state.capturesMouse = io.WantCaptureMouse;
        return;
    }

    detail::drawEditorMenus(state, scene);
    drawDockspace(state);
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
    if (state.showConsole)
    {
        detail::drawConsolePanel(state);
    }
    if (state.showAssets)
    {
        detail::drawAssetsPanel(state, scene);
    }
    detail::drawEditorPopups(state, scene);
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

void handleShortcuts(ToolsState& state, scene::Scene& scene)
{
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

} // namespace

core::Result<std::unique_ptr<ToolsOverlay>> ToolsOverlay::create(
    platform::Platform& platform, platform::Window& window, render::Renderer& renderer,
    const std::filesystem::path& settingsFile, ToolsMode mode, const std::filesystem::path& recentProjectsFile)
{
    DEVEX_ASSERT_MSG(ImGui::GetCurrentContext() == nullptr, "only one ToolsOverlay may exist");
    IMGUI_CHECKVERSION();

    auto state = std::make_unique<detail::ToolsState>(platform, window, renderer, mode);
    state->settingsFile = core::toUtf8(settingsFile);
    if (mode == ToolsMode::Editor)
    {
        state->visible = true;
        detail::loadRecentProjects(*state, recentProjectsFile);
    }

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = state->settingsFile.c_str();
    applyStyle();

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

void ToolsOverlay::update(scene::Scene& scene, core::Duration frameDelta, PlayState playState)
{
    ToolsState& state = *m_state;
    state.frameTimes.record(static_cast<float>(frameDelta.count() * 1000.0));
    if (!state.visible)
    {
        return;
    }

    state.platform.beginImGuiFrame();
    state.renderer.beginImGuiFrame();
    ImGui::NewFrame();

    if (state.mode == ToolsMode::Editor)
    {
        updateEditor(state, scene, playState);
        return;
    }

    drawMainMenu(state, scene);
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
    if (state.showConsole)
    {
        detail::drawConsolePanel(state);
    }
    if (state.showAssets)
    {
        detail::drawAssetsPanel(state, scene);
    }
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
    return std::exchange(m_state->requests, EditorRequests{});
}

bool ToolsOverlay::confirmClose()
{
    ToolsState& state = *m_state;
    if (state.mode != ToolsMode::Editor || state.database == nullptr || !detail::hasUnsavedChanges(state))
    {
        return true;
    }
    state.pendingChange = detail::SceneChange{detail::SceneChange::Kind::Quit, {}};
    state.openUnsavedChangesPopup = true;
    // The question comes once play has stopped, when the edited scene can be saved.
    state.requests.stop = state.playState != PlayState::Editing;
    return false;
}

void ToolsOverlay::setAssetDatabase(asset::AssetDatabase* database) noexcept
{
    if (m_state->database == database)
    {
        return;
    }
    if (m_state->mode == ToolsMode::Editor && m_state->database != nullptr)
    {
        detail::saveEditorSettings(*m_state);
    }
    m_state->database = database;
    m_state->projectChanged = database != nullptr;
    m_state->selection = core::Uuid{};
    m_state->history.clear();
}

CommandHistory& ToolsOverlay::history() noexcept
{
    return m_state->history;
}

} // namespace devex::tools
