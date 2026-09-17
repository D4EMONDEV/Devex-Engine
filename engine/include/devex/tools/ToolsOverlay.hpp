#pragma once

#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/core/Error.hpp>
#include <devex/core/Time.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/platform/Window.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/render/RenderWorld.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/tools/CommandHistory.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace devex::tools {

namespace detail {
struct ToolsState;
} // namespace detail

enum class ToolsMode : std::uint8_t
{
    // Panels drawn over the running game, shown and hidden with F1.
    Overlay,
    // The editor: the scene is shown in a viewport panel, where it is edited with the mouse and
    // gizmos, saved as project scenes, and played on request. A welcome screen opens projects.
    Editor,
};

enum class PlayState : std::uint8_t
{
    // Gameplay does not run; the editor camera shows the scene being edited.
    Editing,
    // Gameplay runs on a copy of the edited scene, seen through its primary camera.
    Playing,
    // Like Playing, with the simulation stopped.
    Paused,
};

// The state of the project's game code, shown by the editor.
struct GameCodeStatus
{
    enum class State : std::uint8_t
    {
        // The project has no code/ folder.
        None,
        Building,
        // The module is loaded, from the latest successful build.
        Ready,
        Failed,
    };

    State state = State::None;
    // Details, such as the first compilation error.
    std::string message;
};

// What the editor asks of the application, collected while its panels are drawn.
struct EditorRequests
{
    bool play = false;
    bool stop = false;
    bool togglePause = false;
    // Advances one fixed update while paused.
    bool step = false;
    // A .dvxproj file to open in place of the current project.
    std::optional<std::filesystem::path> openProject;
    bool quit = false;
    // Builds the game code now.
    bool buildCode = false;
    // Creates the code/ folder of a project that has none.
    bool createCode = false;
};

// Docked Dear ImGui panels: hierarchy, inspector, assets, statistics and console, over the game or
// around the editor's viewport. Only one instance may exist at a time, since it owns the ImGui
// context.
class ToolsOverlay
{
public:
    // Creates the ImGui context and connects it to the platform and the renderer, which must
    // outlive the overlay. The panel layout is saved to settingsFile. The editor remembers recent
    // projects in recentProjectsFile, by default in the user's data directory.
    [[nodiscard]] static core::Result<std::unique_ptr<ToolsOverlay>> create(
        platform::Platform& platform, platform::Window& window, render::Renderer& renderer,
        const std::filesystem::path& settingsFile, ToolsMode mode = ToolsMode::Overlay,
        const std::filesystem::path& recentProjectsFile = {});

    ~ToolsOverlay();

    ToolsOverlay(const ToolsOverlay&) = delete;
    ToolsOverlay& operator=(const ToolsOverlay&) = delete;

    [[nodiscard]] ToolsMode mode() const noexcept;

    // The editor is always visible.
    [[nodiscard]] bool isVisible() const noexcept;
    void setVisible(bool visible) noexcept;

    // True when the panels use the keyboard or the mouse, which gameplay should then ignore.
    [[nodiscard]] bool capturesKeyboard() const noexcept;
    [[nodiscard]] bool capturesMouse() const noexcept;

    // Builds this frame's panels for the scene and queues them for the renderer's next endFrame.
    // In the editor, the scene is the one being edited, or the copy being played. While hidden,
    // only the frame time statistics are recorded.
    void update(scene::Scene& scene, core::Duration frameDelta, PlayState playState = PlayState::Editing);

    // Editor only: gives the frame the viewport size and, while editing, the editor camera, then
    // adds the grid, light and camera icons, the gizmo, selection outlines and picking. The world
    // transforms of the scene must be up to date.
    void prepareRender(scene::Scene& scene, render::RenderWorld& world, PlayState playState);

    // Editor only: the requests made since the last call.
    [[nodiscard]] EditorRequests takeRequests() noexcept;

    // Editor only: the state of the game code, shown in the menu bar.
    void setGameCodeStatus(GameCodeStatus status);

    // Editor only: whether the application may close now. When the scene has unsaved changes, the
    // editor asks what to do with them first, and requests to quit once they are saved or dropped.
    [[nodiscard]] bool confirmClose();

    // Lists the project assets in the panels; null when there is no project. The database must
    // outlive the overlay or be replaced first. In the editor, a new project opens its last scene
    // on the next update, unless the application already filled the scene.
    void setAssetDatabase(asset::AssetDatabase* database) noexcept;

    [[nodiscard]] CommandHistory& history() noexcept;

private:
    explicit ToolsOverlay(std::unique_ptr<detail::ToolsState> state) noexcept;

    std::unique_ptr<detail::ToolsState> m_state;
};

} // namespace devex::tools
