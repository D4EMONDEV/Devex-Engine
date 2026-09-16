#include "ToolsState.hpp"

#include <devex/core/Assert.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/tools/SceneCommands.hpp>
#include <devex/tools/ToolsOverlay.hpp>

#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
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

void buildDefaultLayout(ImGuiID dockspace, const ImGuiViewport& viewport)
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
    ImGui::DockBuilderDockWindow(detail::consoleWindow, bottom);
    ImGui::DockBuilderDockWindow(detail::statisticsWindow, bottom);
    ImGui::DockBuilderFinish(dockspace);
}

void drawDockspace(ToolsState& state)
{
    const ImGuiViewport* const viewport = ImGui::GetMainViewport();
    const ImGuiID dockspace = ImHashStr("Devex tools dockspace");
    if (state.resetLayout || ImGui::DockBuilderGetNode(dockspace) == nullptr)
    {
        buildDefaultLayout(dockspace, *viewport);
        state.resetLayout = false;
    }
    // The central node stays empty and transparent, showing the game behind the panels.
    ImGui::DockSpaceOverViewport(dockspace, viewport, ImGuiDockNodeFlags_PassthruCentralNode);
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
    const std::filesystem::path& settingsFile)
{
    DEVEX_ASSERT_MSG(ImGui::GetCurrentContext() == nullptr, "only one ToolsOverlay may exist");
    IMGUI_CHECKVERSION();

    auto state = std::make_unique<detail::ToolsState>(platform, renderer);
    state->settingsFile = core::toUtf8(settingsFile);

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
    m_state->renderer.shutdownImGui();
    m_state->platform.shutdownImGui();
    ImGui::DestroyContext();
}

bool ToolsOverlay::isVisible() const noexcept
{
    return m_state->visible;
}

void ToolsOverlay::setVisible(bool visible) noexcept
{
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

void ToolsOverlay::update(scene::Scene& scene, core::Duration frameDelta)
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

CommandHistory& ToolsOverlay::history() noexcept
{
    return m_state->history;
}

} // namespace devex::tools
