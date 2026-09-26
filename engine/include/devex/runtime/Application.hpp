#pragma once

#include <devex/animation/AnimationWorld.hpp>
#include <devex/ui/UiWorld.hpp>
#include <devex/asset/AssetSource.hpp>
#include <devex/audio/AudioWorld.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/core/Error.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/core/Time.hpp>
#include <devex/platform/Event.hpp>
#include <devex/platform/Input.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/platform/Window.hpp>
#include <devex/physics/PhysicsWorld.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/render/RenderWorld.hpp>
#include <devex/runtime/AssetManager.hpp>
#include <devex/scene/Scene.hpp>

#include <concepts>
#include <cstdint>
#include <filesystem>
#include <string>

namespace devex::runtime {

struct ApplicationConfig
{
    std::string title = "Devex";
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    bool resizable = true;
    // Frequency of onFixedUpdate, in hertz.
    std::uint32_t fixedUpdateRate = 60;
    // Upper bound on frames per second; 0 leaves the frame rate unlimited.
    std::uint32_t maxFrameRate = 0;
    // Creates a Vulkan renderer for the main window. Disable it for tools and tests without GPU.
    bool enableRendering = true;
    render::PresentMode presentMode = render::PresentMode::Fifo;
    // Case-insensitive part of the GPU name to use; empty selects the most capable GPU.
    std::string preferredGpu;
    // Makes the tools overlay (hierarchy, inspector, assets, statistics, console) available with
    // F1. Requires rendering.
    bool enableTools = core::assertsEnabled;
    // Runs the application inside the editor: the scene is edited in a viewport and saved as
    // project scenes, and the application's updates only run in Play mode, on a copy of the scene
    // that Stop throws away. Without a project, the editor opens on its welcome screen. Requires
    // rendering; the tools come with it.
    bool editor = false;
    // Loads the game module of the project (its code/ folder, built into .devex/code/<configuration>/bin),
    // runs its systems while the game plays, and loads new builds as they appear. The editor
    // always does, and also builds the module whenever its sources change.
    bool loadGameCode = false;
    // The .dvxproj file whose assets folder is imported and loaded; empty runs without a project.
    std::filesystem::path project;
    // The package of an exported game (.dvxpak), played instead of a project: its assets and
    // settings come from it, and its game module from Game.dll next to the executable. A relative
    // path is relative to the executable.
    std::filesystem::path package;
    // Opens the main window as the settings of the project or package say (name, size, fullscreen,
    // vsync, frame rate limit, icon) instead of the fields above, as the player does.
    bool useProjectWindowSettings = false;
    // Imports again the assets that change on disk while the application runs.
    bool watchAssets = true;
    // Simulates the physics components of the scene while gameplay runs, after the fixed updates,
    // with the physics settings of the project.
    bool enablePhysics = true;
    // Plays the sounds of the scene while gameplay runs, with the audio settings of the project,
    // and lets the editor preview sounds.
    bool enableAudio = true;
    // Sends the sounds to the default output of the system. Without it, or without an output, they
    // are mixed but not heard, as tests need.
    bool audioOutput = true;
    // Worker threads for imports and other jobs; 0 uses every hardware thread but one.
    std::uint32_t workerThreads = 0;
};

namespace detail {
class ApplicationRunner;
} // namespace detail

// Base class of every program driven by the engine loop. Each frame polls events, runs the fixed
// updates that are due (each followed by a physics step), runs one variable update, updates the
// scene transforms, then renders the scene. Engine services are available from onStartup to onShutdown, not in the constructor.
//
// Inside the editor, onStartup and onShutdown run as usual, but the updates and onRender only run
// in Play mode, between onPlayStarted and onPlayStopped, and scene() then returns the copy of the
// edited scene that plays. Entities keep their handles in the copy.
class Application
{
public:
    virtual ~Application() = default;

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Called once the main window exists. An error stops the application before the first frame,
    // and onShutdown is then not called.
    virtual core::Result<void> onStartup()
    {
        return {};
    }

    // Called once after the last frame, while the main window still exists.
    virtual void onShutdown()
    {
    }

    // Called for every platform event, before the updates of the frame.
    virtual void onEvent(const platform::Event& /*event*/)
    {
    }

    // Called zero or more times per frame, at the fixed update rate: physics and gameplay rules.
    virtual void onFixedUpdate(core::Duration /*fixedDelta*/)
    {
    }

    // Called once per frame with the real time elapsed since the previous frame.
    virtual void onUpdate(core::Duration /*frameDelta*/)
    {
    }

    // Called once per rendered frame, after the scene was extracted into the snapshot, to adjust
    // it or draw more. Skipped when rendering is disabled.
    virtual void onRender(render::RenderWorld& /*world*/)
    {
    }

    // Editor only: called when Play starts, once scene() returns the copy that plays.
    virtual void onPlayStarted()
    {
    }

    // Editor only: called when Play stops, while scene() still returns the copy that played.
    virtual void onPlayStopped()
    {
    }

protected:
    Application() = default;

    [[nodiscard]] const platform::Platform& platform() const noexcept;
    [[nodiscard]] const platform::Input& input() const noexcept;
    [[nodiscard]] platform::Window& window() noexcept;
    // The scene rendered every frame. It can be replaced, for instance by a loaded one.
    [[nodiscard]] scene::Scene& scene() noexcept;
    // Loads assets by identifier, with the built-in meshes registered.
    [[nodiscard]] AssetManager& assets() noexcept;
    // The asset database of the project, or null when the application runs without one, as an
    // exported game does.
    [[nodiscard]] asset::AssetDatabase* assetDatabase() noexcept;
    // Where assets come from: the asset database of the project or the package of the game; null
    // without either.
    [[nodiscard]] asset::AssetSource* assetSource() noexcept;
    // The settings of the project or of the exported game; null without either.
    [[nodiscard]] const asset::Project* project() noexcept;
    // Replaces scene() with a scene asset now. While the game runs, the physics starts over and the
    // Start systems run for the new scene. Game systems ask for it with SystemContext::sceneToLoad.
    [[nodiscard]] core::Result<void> loadScene(asset::AssetId scene);
    // Loads the scene in the background, as SystemContext::sceneToLoadInBackground does.
    void loadSceneInBackground(asset::AssetId scene);
    // How much of the scene loading in the background is ready, from 0 to 1; nothing when none is.
    [[nodiscard]] std::optional<float> sceneLoadingProgress() const noexcept;
    [[nodiscard]] core::JobSystem& jobs() noexcept;
    // Only available when ApplicationConfig::enableRendering is set.
    [[nodiscard]] render::Renderer& renderer() noexcept;
    [[nodiscard]] const render::Renderer& renderer() const noexcept;

    // Progress towards the next fixed update in [0, 1), to interpolate between simulation states.
    [[nodiscard]] double interpolationAlpha() const noexcept;
    // The physics world of the scene while gameplay runs; null otherwise, or without physics.
    [[nodiscard]] physics::PhysicsWorld* physics() noexcept;
    // The sounds of the scene while gameplay runs; null otherwise, or without audio.
    [[nodiscard]] audio::AudioWorld* audio() noexcept;
    // The animations of the scene while gameplay runs; null otherwise.
    [[nodiscard]] animation::AnimationWorld* animation() noexcept;
    // The interface of the scene while gameplay runs; null otherwise.
    [[nodiscard]] ui::UiWorld* ui() noexcept;

    // True when the application runs inside the editor.
    [[nodiscard]] bool isEditor() const noexcept;
    // True while gameplay runs: always outside the editor, and in Play mode inside it.
    [[nodiscard]] bool isPlaying() const noexcept;

    // Ends the loop after the current frame. Inside the editor, stops playing instead.
    void requestQuit() noexcept;

private:
    friend class detail::ApplicationRunner;

    detail::ApplicationRunner* m_runner = nullptr;
    platform::Platform* m_platform = nullptr;
    platform::Window* m_window = nullptr;
    render::Renderer* m_renderer = nullptr;
    scene::Scene* m_scene = nullptr;
    AssetManager* m_assets = nullptr;
    core::JobSystem* m_jobs = nullptr;
    physics::PhysicsWorld* m_physics = nullptr;
    audio::AudioWorld* m_audio = nullptr;
    animation::AnimationWorld* m_animation = nullptr;
    ui::UiWorld* m_ui = nullptr;
    double m_interpolationAlpha = 0.0;
    bool m_quitRequested = false;
    bool m_editor = false;
    bool m_playing = true;
    bool m_stopRequested = false;
};

// Runs the application until it requests to quit or its main window is closed. Returns a process
// exit code.
[[nodiscard]] int run(Application& application, const ApplicationConfig& config);

template <std::derived_from<Application> App>
[[nodiscard]] int run(const ApplicationConfig& config)
{
    App application;
    return run(application, config);
}

} // namespace devex::runtime
