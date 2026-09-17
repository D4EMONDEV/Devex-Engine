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
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace devex::tools {

namespace detail {
struct ToolsState;
} // namespace detail

enum class ToolsMode : std::uint8_t
{
    // Panels drawn over the running game, shown and hidden with F1.
    Overlay,
    // The editor: scenes open in tabs and are shown in a viewport panel, where they are edited with
    // the mouse and gizmos, saved as project scenes, and played on request. Without a project, a
    // project manager lists, creates and opens projects.
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

// A build of the engine that games can be exported with, found next to the editor.
struct EngineBuildChoice
{
    // The name of its folder, such as "x64-release".
    std::string name;
    // "Debug" or "Release".
    std::string configuration;
};

// The progress or the outcome of the last export of the game.
struct ExportStatus
{
    enum class State : std::uint8_t
    {
        Idle,
        Running,
        Succeeded,
        Failed,
    };

    State state = State::Idle;
    // The current step, a summary of the result, or the error.
    std::string message;
    // From 0 to 1 while running.
    float fraction = 0.0f;
    std::filesystem::path output;
    std::filesystem::path executable;
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
    // Closes the project and goes back to the project manager.
    bool closeProject = false;
    bool quit = false;
    // Builds the game code now.
    bool buildCode = false;
    // Creates the code/ folder of a project that has none.
    bool createCode = false;
    // Exports the game as the export settings of the project say.
    bool exportGame = false;
};

// Docked Dear ImGui panels in a Godot-like theme: scene tree, inspector, file system, output and
// statistics, over the game or around the editor's viewport. Only one instance may exist at a time,
// since it owns the ImGui context.
class ToolsOverlay
{
public:
    // Creates the ImGui context and connects it to the platform and the renderer, which must
    // outlive the overlay. Fonts and icons come from resources/ next to the executable. The panel
    // layout is saved to settingsFile. The editor keeps the theme and the projects of the user in
    // userSettingsFile, by default in the user's data directory.
    [[nodiscard]] static core::Result<std::unique_ptr<ToolsOverlay>> create(
        platform::Platform& platform, platform::Window& window, render::Renderer& renderer,
        const std::filesystem::path& settingsFile, ToolsMode mode = ToolsMode::Overlay,
        const std::filesystem::path& userSettingsFile = {});

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

    // Editor only: the engine builds that games can be exported with, and the state of the export.
    void setEngineBuilds(std::vector<EngineBuildChoice> builds);
    void setExportStatus(ExportStatus status);

    // Editor only: whether the application may close now. When scenes have unsaved changes, the
    // editor asks what to do with them first, and requests to quit once they are saved or dropped.
    // The scene is the edited one, not a copy being played.
    [[nodiscard]] bool confirmClose(scene::Scene& editedScene);

    // Editor only: calls the function with the scenes of the tabs in the background, which live in
    // the editor rather than in the application.
    void forEachBackgroundScene(const std::function<void(scene::Scene&)>& function);

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
