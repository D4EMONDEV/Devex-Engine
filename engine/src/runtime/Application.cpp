#include <devex/asset/AssetId.hpp>
#include <devex/asset/Package.hpp>
#include <devex/asset/Primitives.hpp>
#include <devex/asset/Project.hpp>
#include <devex/animation/AnimationWorld.hpp>
#include <devex/asset/import/TextureProcessing.hpp>
#include <devex/audio/AudioEngine.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Profiler.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Assert.hpp>
#include <devex/core/BuildInfo.hpp>
#include <devex/core/Log.hpp>
#include "GameCodeBuilder.hpp"
#include "ManagedCodeBuilder.hpp"
#include "ManagedGame.hpp"

#include <devex/platform/Process.hpp>
#include <devex/runtime/Application.hpp>
#include <devex/runtime/ComponentViews.hpp>
#include <devex/runtime/FixedTimestep.hpp>
#include <devex/runtime/GameExport.hpp>
#include <devex/runtime/GameModule.hpp>
#include <devex/scene/AssetReferences.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/runtime/SceneExtraction.hpp>
#include <devex/tools/ToolsOverlay.hpp>

#include <algorithm>
#include <atomic>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <thread>
#include <variant>

namespace devex::runtime {

namespace detail {

struct EngineServices
{
    platform::Platform& platform;
    platform::Window& window;
    render::Renderer* renderer = nullptr;
    scene::Scene& scene;
    AssetManager& assets;
    core::JobSystem& jobs;
    tools::ToolsOverlay* tools = nullptr;
    // The mixer of the process; null without audio.
    audio::AudioEngine* audio = nullptr;
    // The project opened, replaced when the editor opens another one.
    std::unique_ptr<asset::AssetDatabase>& database;
    // The package of an exported game, played instead of a project.
    std::unique_ptr<asset::PackageReader>& package;
};

// Sets the icon of the window from the game's settings: the pixels kept in its package, or the
// source image of the icon texture in a project.
void applyWindowIcon(platform::Window& window, const asset::AssetDatabase* database, const asset::PackageReader* package)
{
    if (package != nullptr)
    {
        const core::Result<std::optional<asset::PackageIcon>> icon = package->icon();
        if (!icon)
        {
            DEVEX_LOG_WARNING("The game has no icon: {}", icon.error());
        }
        else if (*icon)
        {
            window.setIcon((*icon)->rgba, (*icon)->width, (*icon)->height);
        }
        return;
    }
    if (database == nullptr || !database->project().window.icon.isValid())
    {
        return;
    }
    const std::optional<asset::SourceFile> source = database->sourceOf(database->project().window.icon);
    const std::optional<std::filesystem::path> path =
        source ? database->project().absolutePath(source->path) : std::nullopt;
    const core::Result<std::vector<std::byte>> bytes =
        path ? core::readBinaryFile(*path) : core::makeError(core::ErrorCode::NotFound, "the icon texture is missing");
    const core::Result<asset::Image> image =
        bytes ? asset::decodeImage(*bytes) : core::Result<asset::Image>(std::unexpected(bytes.error()));
    if (!image)
    {
        DEVEX_LOG_WARNING("The game has no icon: {}", image.error());
        return;
    }
    window.setIcon(image->rgba, image->width, image->height);
}

[[nodiscard]] core::Result<std::unique_ptr<asset::AssetDatabase>> openProject(const std::filesystem::path& projectFile,
                                                                             core::JobSystem& jobs, bool watchAssets)
{
    const core::Result<asset::Project> project = asset::loadProject(projectFile);
    if (!project)
    {
        return std::unexpected(project.error());
    }
    core::Result<std::unique_ptr<asset::AssetDatabase>> opened =
        asset::AssetDatabase::open(*project, jobs, {.watchFiles = watchAssets});
    if (!opened)
    {
        return core::makeError(opened.error().code, "cannot open the assets of {}: {}", project->name,
                               opened.error().message);
    }
    DEVEX_LOG_INFO("Project {} at {}: {} source files, {} imports queued", project->name,
                   core::toUtf8(project->root), (*opened)->sources().size(), (*opened)->pendingImports());
    return opened;
}

// An export of the game running on its own thread, for the editor.
class ExportJob
{
public:
    explicit ExportJob(ExportPlan plan)
        : m_thread([this, plan = std::move(plan)] { run(plan); })
    {
    }

    // Asks the export to stop, and waits for it.
    ~ExportJob()
    {
        m_cancel = true;
        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }

    ExportJob(const ExportJob&) = delete;
    ExportJob& operator=(const ExportJob&) = delete;

    [[nodiscard]] tools::ExportStatus status() const
    {
        const std::scoped_lock lock(m_mutex);
        return m_status;
    }

    [[nodiscard]] bool finished() const noexcept
    {
        return m_finished;
    }

private:
    void run(const ExportPlan& plan)
    {
        const core::Result<ExportResult> result = exportGame(
            plan,
            [this](const ExportProgress& progress) {
                const std::scoped_lock lock(m_mutex);
                m_status.message = progress.step;
                m_status.fraction = progress.fraction;
            },
            &m_cancel);
        {
            const std::scoped_lock lock(m_mutex);
            if (result)
            {
                m_status = {
                    .state = tools::ExportStatus::State::Succeeded,
                    .message = std::format("Exported in {:.1f} s: {} assets, {:.1f} MB of package", result->seconds,
                                           result->assets, static_cast<double>(result->packageBytes) / (1024.0 * 1024.0)),
                    .fraction = 1.0f,
                    .output = result->output,
                    .executable = result->executable,
                };
                DEVEX_LOG_INFO("Exported {} to {} in {:.1f} s", plan.project.name, core::toUtf8(result->output),
                               result->seconds);
            }
            else
            {
                m_status = {.state = tools::ExportStatus::State::Failed, .message = result.error().message};
                DEVEX_LOG_ERROR("The export failed: {}", result.error());
            }
        }
        m_finished = true;
    }

    mutable std::mutex m_mutex;
    tools::ExportStatus m_status{.state = tools::ExportStatus::State::Running, .message = "Starting the export"};
    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_finished{false};
    // Last, so that it starts once the rest exists.
    std::thread m_thread;
};

class ApplicationRunner
{
public:
    ApplicationRunner(Application& application, const ApplicationConfig& config,
                      const EngineServices& services) noexcept;
    ~ApplicationRunner();

    ApplicationRunner(const ApplicationRunner&) = delete;
    ApplicationRunner& operator=(const ApplicationRunner&) = delete;

    [[nodiscard]] int execute();

    [[nodiscard]] asset::AssetDatabase* database() const noexcept;
    [[nodiscard]] asset::AssetSource* assetSource() const noexcept;
    // Replaces the scene the application sees with a scene asset, restarting the game when it runs.
    [[nodiscard]] core::Result<void> loadScene(asset::AssetId sceneAsset);
    [[nodiscard]] core::Result<scene::Scene> readScene(asset::AssetId sceneAsset);
    // Replaces the scene, restarting the game's worlds and systems when it plays.
    void replaceScene(scene::Scene loaded, asset::AssetId sceneAsset);
    void loadSceneInBackground(asset::AssetId sceneAsset);
    // Replaces the scene with the one loading in the background once what it shows is ready.
    void updateBackgroundScene();
    [[nodiscard]] std::optional<float> sceneLoadingProgress() const noexcept;

private:
    using Clock = std::chrono::steady_clock;

    // A minimized window shows nothing: keep simulating in real time without spinning the CPU.
    static constexpr std::chrono::milliseconds minimizedFrameTime{50};

    [[nodiscard]] bool isEditor() const noexcept;
    void handleEvent(const platform::Event& event);
    // Asks to quit, which the editor may postpone to ask about unsaved changes.
    void requestClose();
    // Runs the updates and the rendering of one frame. Also called from the operating system's
    // modal loop while the window is being resized, where events cannot be polled.
    void runFrame();
    void runGameplay(std::chrono::nanoseconds frameTime);
    // Animation, interface, transforms, physics interpolation and audio, after the gameplay.
    void updateFrameWorlds(std::chrono::nanoseconds frameTime, bool interpolate);
    // Loads the scene that game systems asked for, if any.
    void loadRequestedScene();
    void render(bool gameplay);
    void handleEditorRequests(tools::EditorRequests requests);
    void startPlaying();
    // Starts Play once a C# debugger attaches, and tells the editor about the debugger.
    void updateDebugger();
    void stopPlaying();
    void switchProject(const std::filesystem::path& projectFile);
    // Drops the project and everything loaded from it; the editor shows its project manager.
    void closeProject();
    // Calls the function with every scene the game code may have components in: the edited scene,
    // the played copy and the scenes of the editor's background tabs.
    void forEachScene(const std::function<void(scene::Scene&)>& function);
    // Applies finished imports. In the editor, prefab instances in the edited scenes follow their
    // changed prefabs: they are saved against the previous prefabs, whose texts the asset manager
    // still has, then loaded again with the new ones. A game that plays keeps its instances.
    void handleAssetEvents(asset::AssetDatabase& database);

    // Game code of the project: loaded when a project opens, built by the editor, reloaded when a
    // new build appears.
    void openGameCode();
    // Outside the editor, builds the code of the project when it is out of date, before any of it
    // loads: a module built for another engine shares the layout of engine types and would not
    // survive loading into this one.
    [[nodiscard]] core::Result<void> buildOutdatedGameCode();
    void closeGameCode();
    void loadGameModule();
    void unloadGameModule();
    void updateGameCode();
    // C#: the runtime of the process, then the assembly a project builds from its C# files.
    void openManagedCode();
    // Gives the C# code of the project views of its C++ components, and builds it again when they change.
    void writeGameComponentViews();
    void loadManagedAssembly();
    void unloadManagedAssembly();
    void updateManagedCode();
    void runSystems(SystemPhase phase, core::Duration delta);
    // The physics world lives while gameplay runs: from startup outside the editor, during Play in it.
    void createPhysics();
    void destroyPhysics();
    // The sounds of the game live alongside the physics. Game code may change the volumes of the
    // groups: they start from the project's settings when the game starts, and follow them when
    // the project changes.
    void createAudio();
    void destroyAudio();
    // The animations of the game live alongside its sounds; the editor also poses skeletons
    // outside Play, which the Animation panel drives.
    void createAnimation();
    void destroyAnimation();
    void createUi();
    void destroyUi();
    // The input actions live as long as the game, across its scenes: their contexts and the
    // bindings of the player are the game's, which keeps the bindings in the folder of the user.
    void createInput();
    void destroyInput();
    [[nodiscard]] std::optional<std::filesystem::path> bindingsFile() const;
    void saveBindings();
    [[nodiscard]] std::string sceneName(asset::AssetId scene) const;
    // Where what the player keeps goes, found without making it; nothing without a project.
    [[nodiscard]] std::optional<std::filesystem::path> userDirectory() const;
    // The saves and the settings of the player live as long as the game. The settings apply to
    // the mixer, and to the window outside the editor.
    void createGameData();
    void destroyGameData();
    void applyPlayerSettings();
    // After the updates: settings written, pictures asked for, a saved scene put in place.
    void updateGameData();
    // Writes the pictures of the saves the renderer captured.
    void writeThumbnails();
    void updateUi(std::chrono::nanoseconds frameTime);
    // Appends the canvases of the scene to the frame, over the game.
    void buildUi(render::RenderWorld& world);
    // Before the transforms of the frame: clips advance and pose the bones they drive.
    void updateAnimation(std::chrono::nanoseconds frameTime);
    void applyAudioSettings();
    // After the transforms of the frame are final: sounds follow their entities, and pause with
    // the game in the editor.
    void updateAudio(std::chrono::nanoseconds frameTime);
    [[nodiscard]] tools::GameCodeStatus gameCodeStatus() const;
    // The errors and warnings of the last builds of the C++ and C# code, for the text editor.
    [[nodiscard]] std::vector<tools::CodeDiagnostic> codeDiagnostics() const;
    // Writes the file of a new component and opens it in the code editor of the system.
    void createScript(const tools::NewScript& script);
    // Starts a requested export once imports and builds are done, and shows its progress.
    void updateExport();
    void startExport();

    Application& m_application;
    EngineServices m_services;
    std::uint32_t m_fixedUpdateRate;
    bool m_watchAssets;
    platform::WindowId m_mainWindow;
    FixedTimestep m_timestep;
    core::Duration m_fixedDelta;
    std::chrono::nanoseconds m_frameBudget;
    Clock::time_point m_previousFrame;
    bool m_inFrame = false;
    int m_exitCode = EXIT_SUCCESS;
    tools::PlayState m_playState = tools::PlayState::Editing;
    // The copy of the scene that plays in the editor.
    std::optional<scene::Scene> m_playScene;
    bool m_stepRequested = false;
    bool m_loadGameCode;
    std::unique_ptr<GameModule> m_game;
    std::unique_ptr<ManagedGame> m_managed;
    std::optional<ManagedCodeBuilder> m_managedBuilder;
    std::filesystem::file_time_type m_managedAssemblyTime{};
    // Set when C# code asks the game to end.
    bool m_enablePhysics;
    std::unique_ptr<physics::PhysicsWorld> m_physics;
    std::unique_ptr<audio::AudioWorld> m_audio;
    std::unique_ptr<animation::AnimationWorld> m_animation;
    std::unique_ptr<ui::UiWorld> m_ui;
    std::unique_ptr<InputActions> m_actions;
    std::filesystem::path m_userDirectory;
    std::unique_ptr<SaveGames> m_saves;
    std::unique_ptr<PlayerSettings> m_settings;
    // The scene asset that plays, and the files the captures in flight go to.
    asset::AssetId m_sceneAsset;
    std::map<std::uint64_t, std::filesystem::path> m_thumbnailCaptures;
    // The themes of the scene being edited, applied outside Play so that the 2D screen and the
    // inspector show the interface as the game will.
    ui::ThemeApplier m_editedThemes;
    // The stick of the pad on the previous frame, so that pushing it moves the focus once.
    math::Vec2 m_uiStick{0.0f};
    // The audio settings the mixer has, to follow changes to the project.
    std::optional<asset::AudioSettings> m_audioSettings;
    // The Start systems ran for the scene that plays.
    bool m_gameStarted = false;
    bool m_waitingForDebugger = false;
    // A scene asked for by game systems, loaded once the updates of the frame are done.
    std::optional<asset::AssetId> m_sceneToLoad;
    // A scene read and built, whose assets load before it replaces the current one.
    struct BackgroundScene
    {
        asset::AssetId asset;
        scene::Scene scene;
        std::vector<asset::AssetId> assets;
        float progress = 0.0f;
    };
    std::optional<BackgroundScene> m_backgroundScene;
    std::optional<GameCodeBuilder> m_gameBuilder;
    bool m_apiRebuildAttempted = false;
    bool m_openManagedAfterBuild = false;
    std::string m_gameLoadError;
    std::unique_ptr<ExportJob> m_export;
    bool m_exportWaiting = false;
    // The build of the library that is loaded, and when to look for a newer one.
    std::filesystem::file_time_type m_gameLibraryTime{};
    Clock::time_point m_nextLibraryCheck{};
};

ApplicationRunner::ApplicationRunner(Application& application, const ApplicationConfig& config,
                                     const EngineServices& services) noexcept
    : m_application(application)
    , m_services(services)
    , m_fixedUpdateRate(config.fixedUpdateRate)
    , m_watchAssets(config.watchAssets)
    , m_mainWindow(services.window.id())
    , m_timestep(FixedTimestep::fromRate(config.fixedUpdateRate))
    , m_fixedDelta(m_timestep.step())
    , m_frameBudget(config.maxFrameRate == 0
                        ? std::chrono::nanoseconds::zero()
                        : std::chrono::nanoseconds(std::chrono::seconds(1)) / config.maxFrameRate)
    , m_loadGameCode(config.loadGameCode || config.editor)
    , m_enablePhysics(config.enablePhysics)
    , m_userDirectory(config.userDirectory)
{
    m_application.m_runner = this;
    m_editedThemes.setThemes([this](asset::AssetId id) { return m_services.assets.theme(id); });
    m_application.m_platform = &services.platform;
    m_application.m_window = &services.window;
    m_application.m_renderer = services.renderer;
    m_application.m_scene = &services.scene;
    m_application.m_assets = &services.assets;
    m_application.m_jobs = &services.jobs;
    m_application.m_interpolationAlpha = 0.0;
    m_application.m_quitRequested = false;
    m_application.m_editor = isEditor();
    m_application.m_playing = !isEditor();
    m_application.m_stopRequested = false;
    AssetManager& assets = services.assets;
    scene::setPrefabSourceLoader([&assets](asset::AssetId prefab) { return assets.sceneText(prefab); });
    if (isEditor())
    {
        std::vector<tools::EngineBuildChoice> choices;
        for (const EngineBuild& build : findEngineBuilds(services.platform.baseDirectory()))
        {
            choices.push_back({.name = build.name, .configuration = build.configuration});
        }
        services.tools->setEngineBuilds(std::move(choices));
    }
}

ApplicationRunner::~ApplicationRunner()
{
    // The scenes outlive the runner: they must not keep components of an unloaded module.
    closeGameCode();
    scene::setPrefabSourceLoader({});
    m_services.platform.setLiveRedrawCallback({});
    m_application.m_runner = nullptr;
    m_application.m_platform = nullptr;
    m_application.m_window = nullptr;
    m_application.m_renderer = nullptr;
    m_application.m_scene = nullptr;
    m_application.m_assets = nullptr;
    m_application.m_jobs = nullptr;
}

asset::AssetDatabase* ApplicationRunner::database() const noexcept
{
    return m_services.database.get();
}

asset::AssetSource* ApplicationRunner::assetSource() const noexcept
{
    if (m_services.database != nullptr)
    {
        return m_services.database.get();
    }
    return m_services.package.get();
}

core::Result<scene::Scene> ApplicationRunner::readScene(asset::AssetId sceneAsset)
{
    const core::Result<std::string> text = m_services.assets.sceneText(sceneAsset);
    if (!text)
    {
        return std::unexpected(text.error());
    }
    return scene::loadScene(*text);
}

void ApplicationRunner::loadSceneInBackground(asset::AssetId sceneAsset)
{
    if (m_backgroundScene && m_backgroundScene->asset == sceneAsset)
    {
        return;
    }
    core::Result<scene::Scene> loaded = readScene(sceneAsset);
    if (!loaded)
    {
        DEVEX_LOG_ERROR("Cannot load scene {}: {}", sceneAsset.uuid, loaded.error());
        return;
    }
    BackgroundScene background{.asset = sceneAsset, .scene = std::move(*loaded)};
    background.assets = scene::referencedAssets(background.scene);
    for (const asset::AssetId asset : background.assets)
    {
        m_services.assets.preload(asset);
    }
    m_backgroundScene = std::move(background);
}

void ApplicationRunner::updateBackgroundScene()
{
    if (!m_backgroundScene)
    {
        return;
    }
    const std::size_t ready = static_cast<std::size_t>(
        std::ranges::count_if(m_backgroundScene->assets, [this](asset::AssetId asset) { return m_services.assets.isReady(asset); }));
    m_backgroundScene->progress =
        m_backgroundScene->assets.empty() ? 1.0f : static_cast<float>(ready) / static_cast<float>(m_backgroundScene->assets.size());
    if (ready < m_backgroundScene->assets.size())
    {
        return;
    }
    BackgroundScene background = std::move(*m_backgroundScene);
    m_backgroundScene.reset();
    replaceScene(std::move(background.scene), background.asset);
}

std::optional<float> ApplicationRunner::sceneLoadingProgress() const noexcept
{
    return m_backgroundScene ? std::optional<float>(m_backgroundScene->progress) : std::nullopt;
}

core::Result<void> ApplicationRunner::loadScene(asset::AssetId sceneAsset)
{
    core::Result<scene::Scene> loaded = readScene(sceneAsset);
    if (!loaded)
    {
        return std::unexpected(loaded.error());
    }
    replaceScene(std::move(*loaded), sceneAsset);
    return {};
}

void ApplicationRunner::replaceScene(scene::Scene loaded, asset::AssetId sceneAsset)
{
    const bool restart = m_gameStarted;
    if (restart)
    {
        destroyUi();
        destroyAnimation();
        destroyAudio();
        destroyPhysics();
    }
    *m_application.m_scene = std::move(loaded);
    m_sceneAsset = sceneAsset;
    if (m_saves)
    {
        m_saves->setScene(m_application.m_scene, sceneAsset, sceneName(sceneAsset));
    }
    m_sceneToLoad.reset();
    m_backgroundScene.reset();
    const asset::AssetSource* const source = assetSource();
    const asset::AssetInfo* const info = source != nullptr ? source->find(sceneAsset) : nullptr;
    DEVEX_LOG_INFO("Loaded scene {}", info != nullptr ? info->name : sceneAsset.uuid.toString());
    if (restart)
    {
        createPhysics();
        createAudio();
        createAnimation();
        createUi();
        runSystems(SystemPhase::Start, core::Duration::zero());
    }
}

bool ApplicationRunner::isEditor() const noexcept
{
    return m_services.tools != nullptr && m_services.tools->mode() == tools::ToolsMode::Editor;
}

int ApplicationRunner::execute()
{
    if (core::Result<void> built = buildOutdatedGameCode(); !built)
    {
        DEVEX_LOG_FATAL("Cannot build the game code: {}", built.error());
        return EXIT_FAILURE;
    }
    // Game components are registered before the application or the editor opens a scene.
    openGameCode();
    if (core::Result<void> started = m_application.onStartup(); !started)
    {
        DEVEX_LOG_FATAL("Application startup failed: {}", started.error());
        return EXIT_FAILURE;
    }
    if (!isEditor())
    {
        createPhysics();
        createAudio();
        createAnimation();
        createUi();
        createInput();
        createGameData();
        m_gameStarted = true;
        runSystems(SystemPhase::Start, core::Duration::zero());
        loadRequestedScene();
    }

    m_services.platform.setLiveRedrawCallback([this] {
        if (!m_inFrame && !m_application.m_quitRequested)
        {
            runFrame();
        }
    });

    m_previousFrame = Clock::now();
    while (!m_application.m_quitRequested)
    {
        core::profiler::beginFrame();
        // Devices used by the tools during the previous frame do not drive gameplay.
        if (m_services.tools != nullptr)
        {
            m_services.platform.setImGuiInputCapture(m_services.tools->capturesKeyboard(),
                                                     m_services.tools->capturesMouse());
        }
        {
            DEVEX_PROFILE_SCOPE("Events");
            m_services.platform.pollEvents(
                [this](const platform::Event& event) { handleEvent(event); });
        }
        if (m_application.m_quitRequested)
        {
            core::profiler::endFrame();
            break;
        }
        runFrame();
        core::profiler::endFrame();
    }

    if (m_playScene)
    {
        stopPlaying();
    }
    m_services.platform.setLiveRedrawCallback({});
    m_application.onShutdown();
    destroyGameData();
    destroyInput();
    destroyUi();
    destroyAnimation();
    destroyAudio();
    destroyPhysics();
    return m_exitCode;
}

void ApplicationRunner::handleEvent(const platform::Event& event)
{
    // Inside the editor, the application only sees events while it plays.
    if (!isEditor() || m_playScene)
    {
        m_application.onEvent(event);
    }
    else if (const auto* dropped = std::get_if<platform::FileDropped>(&event);
             dropped != nullptr && m_services.database != nullptr)
    {
        // Files dropped on the editor are copied into the project, where they are imported.
        const core::Result<asset::AssetId> added =
            m_services.database->addFile(core::pathFromUtf8(dropped->path), "res://assets/imported");
        if (added)
        {
            DEVEX_LOG_INFO("Copied {} into assets/imported", dropped->path);
        }
        else
        {
            DEVEX_LOG_ERROR("Cannot add {}: {}", dropped->path, added.error());
        }
    }

    if (const auto* key = std::get_if<platform::KeyPressed>(&event); key != nullptr && !key->repeat && m_services.tools != nullptr)
    {
        m_services.tools->notifyKeyPressed(key->key);
    }
    if (const auto* key = std::get_if<platform::KeyPressed>(&event);
        key != nullptr && key->key == platform::Key::F1 && !key->repeat &&
        m_services.tools != nullptr && !isEditor())
    {
        const bool visible = !m_services.tools->isVisible();
        m_services.tools->setVisible(visible);
        // The panels need the cursor.
        if (visible && m_services.window.isMouseCaptured())
        {
            m_services.window.setMouseCaptured(false);
        }
    }

    if (std::holds_alternative<platform::QuitRequested>(event))
    {
        requestClose();
    }
    else if (const auto* closeRequest = std::get_if<platform::WindowCloseRequested>(&event);
             closeRequest != nullptr && closeRequest->window == m_mainWindow)
    {
        requestClose();
    }
}

void ApplicationRunner::requestClose()
{
    if (!isEditor() || m_services.tools->confirmClose(m_services.scene))
    {
        m_application.m_quitRequested = true;
    }
}

void ApplicationRunner::runFrame()
{
    m_inFrame = true;

    const Clock::time_point frameStart = Clock::now();
    const std::chrono::nanoseconds frameTime = frameStart - m_previousFrame;
    m_previousFrame = frameStart;

    // Finished imports replace the assets they changed before anything uses them this frame, and
    // the meshes and textures the workers loaded reach the renderer.
    {
        DEVEX_PROFILE_SCOPE("Assets");
        if (m_services.database != nullptr)
        {
            handleAssetEvents(*m_services.database);
        }
        m_services.assets.finishLoads();
        writeThumbnails();
    }

    {
        DEVEX_PROFILE_SCOPE("Game code");
        updateGameCode();
    }

    const bool minimized = m_services.window.isMinimized();
    const bool canRender = m_services.renderer != nullptr && !minimized;
    if (isEditor())
    {
        // The editor goes first, so that Play and Stop take effect this frame.
        m_services.tools->setGameCodeStatus(gameCodeStatus());
        m_services.tools->setCodeDiagnostics(codeDiagnostics());
        updateDebugger();
        updateExport();
        if (canRender)
        {
            DEVEX_PROFILE_SCOPE("Editor");
            m_services.tools->update(*m_application.m_scene, core::Duration(frameTime), m_playState);
        }
        handleEditorRequests(m_services.tools->takeRequests());
        if (m_playState == tools::PlayState::Playing)
        {
            runGameplay(frameTime);
        }
        else if (m_playState == tools::PlayState::Paused && std::exchange(m_stepRequested, false))
        {
            runGameplay(m_timestep.step());
        }
        updateFrameWorlds(frameTime, m_playScene.has_value());
        if (canRender && !m_application.m_quitRequested)
        {
            render(m_playScene.has_value());
        }
    }
    else
    {
        runGameplay(frameTime);
        updateFrameWorlds(frameTime, true);
        if (canRender && !m_application.m_quitRequested)
        {
            if (m_services.tools != nullptr)
            {
                DEVEX_PROFILE_SCOPE("Tools");
                m_services.tools->update(*m_application.m_scene, core::Duration(frameTime));
            }
            render(true);
        }
    }

    const std::chrono::nanoseconds frameLimit =
        minimized ? std::max(m_frameBudget, std::chrono::nanoseconds(minimizedFrameTime))
                  : m_frameBudget;
    if (frameLimit > std::chrono::nanoseconds::zero())
    {
        DEVEX_PROFILE_SCOPE("Frame limit");
        platform::sleepPrecise(frameLimit - (Clock::now() - frameStart));
    }

    m_inFrame = false;
}

void ApplicationRunner::updateFrameWorlds(std::chrono::nanoseconds frameTime, bool interpolate)
{
    {
        DEVEX_PROFILE_SCOPE("Animation");
        updateAnimation(frameTime);
    }
    {
        DEVEX_PROFILE_SCOPE("Interface");
        updateUi(frameTime);
        if (isEditor() && !m_playScene)
        {
            m_editedThemes.apply(*m_application.m_scene);
        }
    }
    {
        DEVEX_PROFILE_SCOPE("Transforms");
        m_application.m_scene->updateTransforms();
    }
    if (m_physics && interpolate)
    {
        DEVEX_PROFILE_SCOPE("Physics interpolation");
        m_physics->interpolate(*m_application.m_scene, static_cast<float>(m_timestep.alpha()));
    }
    {
        DEVEX_PROFILE_SCOPE("Audio");
        updateAudio(frameTime);
    }
}

void ApplicationRunner::runGameplay(std::chrono::nanoseconds frameTime)
{
    DEVEX_PROFILE_SCOPE("Gameplay");
    if (m_actions)
    {
        m_actions->update(m_services.platform.input(), m_ui && m_ui->isEditing());
    }
    if (m_saves)
    {
        m_saves->addPlayTime(std::chrono::duration<double>(frameTime).count());
    }
    const std::uint32_t steps = m_timestep.advance(frameTime);
    for (std::uint32_t step = 0; step < steps; ++step)
    {
        DEVEX_PROFILE_SCOPE("Fixed step");
        m_application.onFixedUpdate(m_fixedDelta);
        runSystems(SystemPhase::FixedUpdate, m_fixedDelta);
        if (m_physics)
        {
            DEVEX_PROFILE_SCOPE("Physics");
            m_application.m_scene->updateTransforms();
            m_physics->step(*m_application.m_scene, m_fixedDelta);
        }
    }
    m_application.m_interpolationAlpha = m_timestep.alpha();
    // The application may replace the scene during its updates.
    DEVEX_PROFILE_SCOPE("Update");
    m_application.onUpdate(core::Duration(frameTime));
    runSystems(SystemPhase::Update, core::Duration(frameTime));
    if (m_actions && m_actions->takeChanges())
    {
        saveBindings();
    }
    updateGameData();
    // The contacts of this frame's steps have been seen by the updates.
    if (m_physics)
    {
        m_physics->clearContacts();
    }

    if (std::exchange(m_application.m_stopRequested, false) && m_playScene)
    {
        stopPlaying();
        return;
    }
    loadRequestedScene();
}

void ApplicationRunner::loadRequestedScene()
{
    if (const std::optional<asset::AssetId> sceneAsset = std::exchange(m_sceneToLoad, std::nullopt))
    {
        if (core::Result<void> loaded = loadScene(*sceneAsset); !loaded)
        {
            DEVEX_LOG_ERROR("Cannot load scene {}: {}", sceneAsset->uuid, loaded.error());
        }
        return;
    }
    updateBackgroundScene();
}

void ApplicationRunner::render(bool gameplay)
{
    DEVEX_PROFILE_SCOPE("Render");
    render::Renderer* const renderer = m_services.renderer;
    scene::Scene& scene = *m_application.m_scene;
    render::RenderWorld& world = renderer->beginFrame();
    {
        DEVEX_PROFILE_SCOPE("Scene extraction");
        extractScene(scene, m_services.assets, world);
        if (gameplay)
        {
            m_application.onRender(world);
        }
    }
    if (isEditor())
    {
        DEVEX_PROFILE_SCOPE("Editor overlays");
        m_services.tools->prepareRender(scene, world, m_playState);
    }
    // The interface is drawn last, over the game and its overlays.
    {
        DEVEX_PROFILE_SCOPE("Interface drawing");
        buildUi(world);
    }
    if (core::Result<void> rendered = renderer->endFrame(); !rendered)
    {
        DEVEX_LOG_FATAL("Rendering failed: {}", rendered.error());
        m_application.m_quitRequested = true;
        m_exitCode = EXIT_FAILURE;
    }
}

void ApplicationRunner::handleEditorRequests(tools::EditorRequests requests)
{
    if (requests.quit)
    {
        m_application.m_quitRequested = true;
        return;
    }
    if (requests.openProject)
    {
        switchProject(*requests.openProject);
        return;
    }
    if (requests.closeProject)
    {
        closeProject();
        return;
    }
    if (requests.createCode && m_services.database != nullptr)
    {
        const asset::Project& project = m_services.database->project();
        if (core::Result<void> created = GameCodeBuilder::createCode(project); !created)
        {
            DEVEX_LOG_ERROR("Cannot create the game code: {}", created.error());
        }
        else
        {
            DEVEX_LOG_INFO("Created {}: the editor builds it now", core::toUtf8(project.codeDirectory()));
            closeGameCode();
            openGameCode();
        }
    }
    if (requests.buildCode && m_gameBuilder)
    {
        m_gameBuilder->requestBuild();
    }
    if (requests.exportGame && m_services.database != nullptr && !m_export)
    {
        m_exportWaiting = true;
    }
    if (requests.newScript && m_services.database != nullptr)
    {
        createScript(*requests.newScript);
    }
    if (requests.play && !m_playScene && m_services.database != nullptr)
    {
        if (requests.waitForDebugger && m_managed && m_managed->hasAssembly() && !m_managed->isDebuggerAttached())
        {
            m_waitingForDebugger = true;
            DEVEX_LOG_INFO("Waiting for a C# debugger to attach to process {} before playing",
                           platform::currentProcessId());
        }
        else
        {
            startPlaying();
        }
    }
    else if (requests.stop && m_waitingForDebugger)
    {
        m_waitingForDebugger = false;
        DEVEX_LOG_INFO("No longer waiting for a debugger");
    }
    else if (requests.stop && m_playScene)
    {
        stopPlaying();
    }
    if (requests.togglePause && m_playScene)
    {
        m_playState = m_playState == tools::PlayState::Paused ? tools::PlayState::Playing : tools::PlayState::Paused;
    }
    if (requests.step && m_playState == tools::PlayState::Paused)
    {
        m_stepRequested = true;
    }
}

void ApplicationRunner::updateDebugger()
{
    const bool available = m_managed && m_managed->hasAssembly();
    const bool attached = available && m_managed->isDebuggerAttached();
    if (m_waitingForDebugger && (!available || attached || m_playScene))
    {
        m_waitingForDebugger = false;
        if (attached && !m_playScene && m_services.database != nullptr)
        {
            DEVEX_LOG_INFO("A C# debugger is attached");
            startPlaying();
        }
    }
    m_services.tools->setDebuggerStatus({
        .available = available,
        .attached = attached,
        .waiting = m_waitingForDebugger,
        .processId = platform::currentProcessId(),
    });
}

void ApplicationRunner::startPlaying()
{
    const tools::GameCodeStatus status = gameCodeStatus();
    if (status.state == tools::GameCodeStatus::State::Building || status.state == tools::GameCodeStatus::State::Failed)
    {
        DEVEX_LOG_WARNING("Cannot play yet: {}", status.message);
        return;
    }
    m_playScene.emplace(m_services.scene.clone());
    m_application.m_scene = &*m_playScene;
    m_application.m_playing = true;
    m_application.m_stopRequested = false;
    m_playState = tools::PlayState::Playing;
    // The simulation starts from a whole step, without the time spent editing.
    m_timestep = FixedTimestep::fromRate(m_fixedUpdateRate);
    createPhysics();
    createAudio();
    createAnimation();
    createUi();
    createInput();
    if (m_services.database != nullptr)
    {
        const std::string edited = m_services.database->project().resourcePath(m_services.tools->scenePath());
        m_sceneAsset = edited.empty() ? asset::AssetId{} : m_services.database->findByPath(edited).value_or(asset::AssetId{});
    }
    createGameData();
    DEVEX_LOG_INFO("Playing");
    m_application.onPlayStarted();
    m_gameStarted = true;
    runSystems(SystemPhase::Start, core::Duration::zero());
    loadRequestedScene();
}

void ApplicationRunner::stopPlaying()
{
    m_application.onPlayStopped();
    destroyGameData();
    destroyInput();
    destroyUi();
    destroyAnimation();
    destroyAudio();
    destroyPhysics();
    m_gameStarted = false;
    m_sceneToLoad.reset();
    m_backgroundScene.reset();
    m_application.m_scene = &m_services.scene;
    m_application.m_playing = false;
    m_application.m_stopRequested = false;
    m_playState = tools::PlayState::Editing;
    m_playScene.reset();
    m_stepRequested = false;
    if (m_services.window.isMouseCaptured())
    {
        m_services.window.setMouseCaptured(false);
    }
    DEVEX_LOG_INFO("Stopped playing");
}

void ApplicationRunner::closeProject()
{
    if (m_playScene)
    {
        stopPlaying();
    }
    // Everything loaded from the project goes before its database.
    m_export.reset();
    m_exportWaiting = false;
    m_services.tools->setExportStatus({});
    m_services.tools->setAssetDatabase(nullptr);
    closeGameCode();
    m_services.assets.setSource(nullptr);
    m_services.database.reset();
    m_services.scene = scene::Scene{};
}

void ApplicationRunner::forEachScene(const std::function<void(scene::Scene&)>& function)
{
    function(m_services.scene);
    if (m_playScene)
    {
        function(*m_playScene);
    }
    if (isEditor())
    {
        m_services.tools->forEachBackgroundScene(function);
    }
}

void ApplicationRunner::handleAssetEvents(asset::AssetDatabase& database)
{
    const std::vector<asset::AssetEvent> events = database.update();
    std::vector<asset::AssetId> scenes;
    for (const asset::AssetEvent& event : events)
    {
        if (event.type == asset::AssetType::Scene)
        {
            scenes.push_back(event.id);
        }
    }

    struct EditedScene
    {
        scene::Scene* scene;
        std::vector<scene::PrefabInstanceSnapshot> snapshots;
    };
    std::vector<EditedScene> edited;
    if (!scenes.empty() && isEditor())
    {
        const auto snapshot = [&](scene::Scene& scene) {
            if (std::vector<scene::PrefabInstanceSnapshot> snapshots = scene::snapshotPrefabInstances(scene, scenes);
                !snapshots.empty())
            {
                edited.push_back({&scene, std::move(snapshots)});
            }
        };
        snapshot(m_services.scene);
        m_services.tools->forEachBackgroundScene(snapshot);
    }

    m_services.assets.handleEvents(events);

    std::size_t rebuilt = 0;
    for (EditedScene& scene : edited)
    {
        rebuilt += scene::rebuildPrefabInstances(*scene.scene, scene.snapshots);
    }
    if (rebuilt > 0)
    {
        DEVEX_LOG_INFO("Updated {} prefab instance{}", rebuilt, rebuilt == 1 ? "" : "s");
    }
}

void ApplicationRunner::switchProject(const std::filesystem::path& projectFile)
{
    closeProject();

    core::Result<std::unique_ptr<asset::AssetDatabase>> opened = openProject(projectFile, m_services.jobs, m_watchAssets);
    if (!opened)
    {
        DEVEX_LOG_ERROR("Cannot open the project: {}", opened.error());
        return;
    }
    m_services.database = std::move(*opened);
    m_services.assets.setSource(m_services.database.get());
    openGameCode();
    m_services.tools->setAssetDatabase(m_services.database.get());
}

void ApplicationRunner::openManagedCode()
{
    const asset::Project& project = assetSource()->project();
    if (!m_managed)
    {
        core::Result<std::unique_ptr<ManagedGame>> managed =
            ManagedGame::create(m_services.platform.baseDirectory() / "managed");
        if (!managed)
        {
            // Games written in C++ do not need any of this.
            DEVEX_LOG_DEBUG("C# is unavailable: {}", managed.error());
            return;
        }
        m_managed = std::move(*managed);
    }
    if (isEditor() && m_services.database != nullptr && ManagedCodeBuilder::hasCode(project))
    {
        m_managedBuilder.emplace(project, m_services.platform.baseDirectory() / "managed");
    }
    loadManagedAssembly();
}

void ApplicationRunner::loadManagedAssembly()
{
    if (!m_managed)
    {
        return;
    }
    const bool packaged = m_services.database == nullptr;
    const std::filesystem::path assembly = packaged
                                               ? m_services.platform.baseDirectory() / "Game.Scripts.dll"
                                               : ManagedCodeBuilder::assemblyPath(assetSource()->project());
    std::error_code error;
    const std::filesystem::file_time_type built = std::filesystem::last_write_time(assembly, error);
    if (error)
    {
        return;
    }
    m_managedAssemblyTime = built;
    if (core::Result<void> loaded = m_managed->loadAssembly(assembly); !loaded)
    {
        DEVEX_LOG_ERROR("Cannot load the C# code: {}", loaded.error());
        return;
    }
    std::size_t restored = 0;
    forEachScene([&restored](scene::Scene& scene) { restored += scene::restorePreservedComponents(scene); });
    DEVEX_LOG_INFO("C# code loaded: {} components{}", m_managed->componentTypes().size(),
                   restored > 0 ? std::format(", {} components restored", restored) : std::string());
}

void ApplicationRunner::unloadManagedAssembly()
{
    if (!m_managed || !m_managed->hasAssembly())
    {
        return;
    }
    // The components stay in the scenes as text until the code comes back.
    forEachScene([this](scene::Scene& scene) { static_cast<void>(m_managed->release(scene)); });
    m_managed->unloadAssembly();
}

void ApplicationRunner::updateManagedCode()
{
    if (!m_managed)
    {
        return;
    }
    bool reload = m_managedBuilder && m_managedBuilder->update();
    // A build made outside the editor is loaded too, but not the editor's own before it is over.
    const bool building = m_managedBuilder && m_managedBuilder->state() == ManagedCodeBuilder::State::Building;
    if (!reload && !building && m_services.database != nullptr)
    {
        std::error_code error;
        const std::filesystem::file_time_type built =
            std::filesystem::last_write_time(ManagedCodeBuilder::assemblyPath(assetSource()->project()), error);
        reload = !error && built != m_managedAssemblyTime;
    }
    if (reload)
    {
        unloadManagedAssembly();
        loadManagedAssembly();
    }
}

core::Result<void> ApplicationRunner::buildOutdatedGameCode()
{
    if (isEditor() || !m_loadGameCode || m_services.database == nullptr)
    {
        return {};
    }
    const asset::Project& project = m_services.database->project();
    if (GameCodeBuilder::hasCode(project))
    {
        GameCodeBuilder builder(project, (m_services.platform.baseDirectory() / ".." / "cmake").lexically_normal());
        if (builder.pending())
        {
            DEVEX_LOG_INFO("The C++ code of {} is out of date: building it before playing", project.name);
            if (core::Result<void> built = builder.buildAndWait(); !built)
            {
                return built;
            }
        }
    }
    if (ManagedCodeBuilder::hasCode(project))
    {
        ManagedCodeBuilder builder(project, m_services.platform.baseDirectory() / "managed");
        if (builder.needsBuild())
        {
            DEVEX_LOG_INFO("The C# code of {} is out of date: building it before playing", project.name);
            if (core::Result<void> built = builder.buildAndWait(); !built)
            {
                return built;
            }
        }
    }
    return {};
}

void ApplicationRunner::openGameCode()
{
    if (!m_loadGameCode || assetSource() == nullptr)
    {
        return;
    }
    if (m_services.database == nullptr)
    {
        // An exported game ships its module next to the executable.
        openManagedCode();
        loadGameModule();
        return;
    }
    const asset::Project& project = m_services.database->project();
    if (isEditor() && GameCodeBuilder::hasCode(project))
    {
        m_gameBuilder.emplace(project, (m_services.platform.baseDirectory() / ".." / "cmake").lexically_normal());
    }
    if (m_gameBuilder && m_gameBuilder->pending())
    {
        DEVEX_LOG_INFO("{}", m_gameBuilder->message());
        // Components remain preserved as scene data until the new module is ready.
        // Start the rebuild before attempting to load an old ABI or its C# component views.
        std::error_code error;
        m_gameLibraryTime = std::filesystem::last_write_time(GameCodeBuilder::libraryPath(project), error);
        m_openManagedAfterBuild = true;
        static_cast<void>(m_gameBuilder->update());
        return;
    }
    openManagedCode();
    loadGameModule();
}

void ApplicationRunner::closeGameCode()
{
    m_gameBuilder.reset();
    m_apiRebuildAttempted = false;
    m_openManagedAfterBuild = false;
    m_gameLoadError.clear();
    unloadGameModule();
    m_managedBuilder.reset();
    unloadManagedAssembly();
}

void ApplicationRunner::loadGameModule()
{
    const bool packaged = m_services.database == nullptr;
    const asset::Project& project = assetSource()->project();
    // An exported game loads its module in place, since it is never rebuilt while the game runs.
    const std::filesystem::path library = packaged
                                              ? m_services.platform.baseDirectory() / GameCodeBuilder::libraryFileName()
                                              : GameCodeBuilder::libraryPath(project);
    std::error_code error;
    const std::filesystem::file_time_type built = std::filesystem::last_write_time(library, error);
    if (error)
    {
        if (!packaged && GameCodeBuilder::hasCode(project) && !isEditor())
        {
            DEVEX_LOG_WARNING("The game code of {} is not built: open the project in devex-editor to build it",
                              project.name);
        }
        return;
    }
    m_gameLibraryTime = built;
    core::Result<std::unique_ptr<GameModule>> module =
        GameModule::load(library, packaged ? std::filesystem::path() : project.cacheDirectory() / "code" / "modules");
    if (!module)
    {
        m_gameLoadError = module.error().message;
        if (m_gameBuilder && !m_apiRebuildAttempted &&
            (module.error().code == core::ErrorCode::Unsupported || module.error().code == core::ErrorCode::Platform))
        {
            m_apiRebuildAttempted = true;
            m_gameBuilder->requestRebuild();
            DEVEX_LOG_WARNING("The cached game code cannot load into this engine; rebuilding it automatically: {}", module.error());
            return;
        }
        DEVEX_LOG_ERROR("Cannot load the game code: {}", module.error());
        return;
    }
    m_gameLoadError.clear();
    m_game = std::move(*module);
    std::size_t restored = 0;
    forEachScene([&restored](scene::Scene& scene) { restored += scene::restorePreservedComponents(scene); });
    DEVEX_LOG_INFO("Game code loaded: {} components, {} systems{}", m_game->registry().components().size(),
                   m_game->registry().systems().size(),
                   restored > 0 ? std::format(", {} components restored", restored) : std::string());
    writeGameComponentViews();
}

void ApplicationRunner::writeGameComponentViews()
{
    if (!isEditor() || m_services.database == nullptr || !m_game)
    {
        return;
    }
    const asset::Project& project = m_services.database->project();
    if (!ManagedCodeBuilder::hasCode(project))
    {
        return;
    }
    std::vector<const reflection::TypeInfo*> types;
    for (const std::string& name : m_game->registry().components())
    {
        if (const scene::ComponentType* const type = scene::componentRegistry().find(name))
        {
            types.push_back(type->type);
        }
    }
    const std::string text = generateComponentViews(types, {}, std::format("the C++ code of {}", project.name));
    const std::filesystem::path file = ManagedCodeBuilder::generatedDirectory(project) / "GameComponents.g.cs";
    if (const core::Result<std::string> existing = core::readTextFile(file); existing && *existing == text)
    {
        return;
    }
    if (core::Result<void> written = core::writeTextFile(file, text); !written)
    {
        DEVEX_LOG_WARNING("Cannot write the C# views of the C++ components: {}", written.error());
        return;
    }
    // The C# code sees the C++ components as they are now.
    DEVEX_LOG_DEBUG("C# views of {} C++ components written", types.size());
    if (m_managedBuilder)
    {
        m_managedBuilder->requestBuild();
    }
}

void ApplicationRunner::unloadGameModule()
{
    if (!m_game)
    {
        return;
    }
    // Components of the module stay in the scenes as text until it comes back.
    forEachScene([this](scene::Scene& scene) { static_cast<void>(m_game->release(scene)); });
    m_game.reset();
}

void ApplicationRunner::updateGameCode()
{
    if (m_services.database == nullptr)
    {
        return;
    }
    bool reload = m_gameBuilder && m_gameBuilder->update();
    const Clock::time_point now = Clock::now();
    if (!reload && m_loadGameCode && now >= m_nextLibraryCheck &&
        (!m_gameBuilder || !m_gameBuilder->pending()))
    {
        // A build made outside the editor, such as from an IDE, is loaded too.
        m_nextLibraryCheck = now + std::chrono::milliseconds(500);
        std::error_code error;
        const std::filesystem::file_time_type built =
            std::filesystem::last_write_time(GameCodeBuilder::libraryPath(m_services.database->project()), error);
        reload = !error && built != m_gameLibraryTime;
    }
    if (reload)
    {
        unloadGameModule();
        loadGameModule();
        if (m_game && m_openManagedAfterBuild)
        {
            m_openManagedAfterBuild = false;
            openManagedCode();
            writeGameComponentViews();
            if (m_managedBuilder)
            {
                // The engine and the native views may have changed even when no .cs file did.
                m_managedBuilder->requestBuild();
            }
        }
    }
    if (!m_gameBuilder || (!m_gameBuilder->pending() && m_game))
    {
        updateManagedCode();
    }
}

void ApplicationRunner::runSystems(SystemPhase phase, core::Duration delta)
{
    if (!m_game && (!m_managed || !m_managed->hasAssembly()))
    {
        return;
    }
    SystemContext context{
        .scene = *m_application.m_scene,
        .input = m_services.platform.input(),
        .window = m_services.window,
        .assets = m_services.assets,
        .actions = m_actions.get(),
        .saves = m_saves.get(),
        .settings = m_settings.get(),
        .physics = m_physics.get(),
        .audio = m_audio.get(),
        .animation = m_animation.get(),
        .ui = m_ui.get(),
        .delta = delta,
        .interpolationAlpha = m_timestep.alpha(),
        .loadingScene = m_backgroundScene ? m_backgroundScene->asset : asset::AssetId{},
        .loadingProgress = m_backgroundScene ? m_backgroundScene->progress : 0.0f,
    };
    if (m_game)
    {
        m_game->registry().run(phase, context);
    }
    if (m_managed)
    {
        ManagedGame::Frame frame{
            .scene = m_application.m_scene,
            .delta = delta,
            .input = &m_services.platform.input(),
            .window = &m_services.window,
            .physics = m_physics.get(),
            .audio = m_audio.get(),
            .animation = m_animation.get(),
            .ui = m_ui.get(),
            .actions = m_actions.get(),
            .saves = m_saves.get(),
            .settings = m_settings.get(),
            .assets = m_services.assets.source(),
            .assetManager = &m_services.assets,
            .loadingScene = context.loadingScene,
            .loadingProgress = context.loadingScene.isValid() ? context.loadingProgress : -1.0f,
        };
        m_managed->runPhase(frame, phase);
        context.quitRequested = context.quitRequested || frame.quitRequested;
        if (frame.sceneToLoad.isValid())
        {
            context.sceneToLoad = frame.sceneToLoad;
        }
        if (frame.sceneToLoadInBackground.isValid())
        {
            context.sceneToLoadInBackground = frame.sceneToLoadInBackground;
        }
    }
    if (context.quitRequested)
    {
        m_application.requestQuit();
    }
    if (context.sceneToLoad.isValid())
    {
        m_sceneToLoad = context.sceneToLoad;
    }
    if (context.sceneToLoadInBackground.isValid())
    {
        loadSceneInBackground(context.sceneToLoadInBackground);
    }
}

void ApplicationRunner::createPhysics()
{
    if (!m_enablePhysics || m_physics)
    {
        return;
    }
    core::Result<std::unique_ptr<physics::PhysicsWorld>> world = physics::PhysicsWorld::create({
        .settings = assetSource() != nullptr ? assetSource()->project().physics : asset::PhysicsSettings{},
        .meshes = [this](asset::AssetId mesh) { return m_services.assets.meshData(mesh); },
    });
    if (!world)
    {
        DEVEX_LOG_ERROR("The game runs without physics: {}", world.error());
        return;
    }
    m_physics = std::move(*world);
    m_application.m_physics = m_physics.get();
}

void ApplicationRunner::destroyPhysics()
{
    m_application.m_physics = nullptr;
    m_physics.reset();
}

void ApplicationRunner::createAudio()
{
    if (m_services.audio == nullptr || m_audio)
    {
        return;
    }
    // Volumes changed by the game that played before do not carry over.
    m_audioSettings.reset();
    applyAudioSettings();
    m_audio = std::make_unique<audio::AudioWorld>(
        *m_services.audio, [this](asset::AssetId clip) { return m_services.assets.audioClip(clip); });
    m_application.m_audio = m_audio.get();
}

void ApplicationRunner::destroyAudio()
{
    m_application.m_audio = nullptr;
    m_audio.reset();
}

void ApplicationRunner::createAnimation()
{
    if (m_animation)
    {
        return;
    }
    m_animation = std::make_unique<animation::AnimationWorld>(
        [this](asset::AssetId clip) { return m_services.assets.animationClip(clip); });
    m_application.m_animation = m_animation.get();
    if (m_services.tools != nullptr)
    {
        m_services.tools->setAnimationWorld(m_animation.get());
    }
}

void ApplicationRunner::createUi()
{
    if (m_ui)
    {
        return;
    }
    m_ui = std::make_unique<ui::UiWorld>();
    // The fields measure their own text, so that a click lands between the right letters.
    m_ui->setFonts([this](asset::AssetId id) {
        const LoadedFont* const font = m_services.assets.font(id);
        return font != nullptr ? ui::FontRef{.data = font->data.get(), .atlas = font->atlas}
                               : ui::FontRef{};
    });
    // The themes are kept by the asset manager: the world only reads the one a canvas names.
    m_ui->setThemes([this](asset::AssetId id) { return m_services.assets.theme(id); });
    m_application.m_ui = m_ui.get();
}

void ApplicationRunner::destroyUi()
{
    m_application.m_ui = nullptr;
    m_ui.reset();
}

void ApplicationRunner::createInput()
{
    if (m_actions)
    {
        return;
    }
    const asset::AssetSource* const source = assetSource();
    m_actions = std::make_unique<InputActions>(source != nullptr ? source->project().input : asset::InputSettings{});
    const platform::Platform& platform = m_services.platform;
    m_actions->setKeyLabeler([&platform](platform::Key key) { return platform.keyLabel(key); });
    if (const std::optional<std::filesystem::path> file = bindingsFile())
    {
        std::error_code error;
        if (std::filesystem::exists(*file, error))
        {
            if (const core::Result<std::string> text = core::readTextFile(*file))
            {
                m_actions->readOverrides(*text);
            }
            else
            {
                DEVEX_LOG_WARNING("Cannot read the key bindings of the player: {}", text.error());
            }
        }
    }
    m_application.m_actions = m_actions.get();
}

void ApplicationRunner::destroyInput()
{
    m_application.m_actions = nullptr;
    m_actions.reset();
}

std::optional<std::filesystem::path> ApplicationRunner::bindingsFile() const
{
    // A game without actions has no bindings.
    const asset::AssetSource* const source = assetSource();
    if (source == nullptr || source->project().input.actions.empty())
    {
        return std::nullopt;
    }
    const std::optional<std::filesystem::path> directory = userDirectory();
    return directory ? std::optional(*directory / "input.dvx") : std::nullopt;
}

std::optional<std::filesystem::path> ApplicationRunner::userDirectory() const
{
    if (!m_userDirectory.empty())
    {
        return m_userDirectory;
    }
    const asset::AssetSource* const source = assetSource();
    if (source == nullptr)
    {
        return std::nullopt;
    }
    // Found without making it: nothing is written there until the player keeps something.
    const core::Result<std::filesystem::path> directory = platform::userDataLocation("", source->project().name);
    if (!directory)
    {
        DEVEX_LOG_WARNING("What the player keeps cannot be kept: {}", directory.error());
        return std::nullopt;
    }
    return *directory;
}

std::string ApplicationRunner::sceneName(asset::AssetId scene) const
{
    const asset::AssetSource* const source = assetSource();
    const asset::AssetInfo* const info = source != nullptr && scene.isValid() ? source->find(scene) : nullptr;
    return info != nullptr ? info->name : std::string();
}

void ApplicationRunner::createGameData()
{
    if (m_saves)
    {
        return;
    }
    const std::optional<std::filesystem::path> directory = userDirectory();
    m_saves = std::make_unique<SaveGames>(directory ? *directory / "saves" : std::filesystem::path());
    m_saves->setAssets(&m_services.assets);
    m_saves->setScene(m_application.m_scene, m_sceneAsset, sceneName(m_sceneAsset));
    m_settings = std::make_unique<PlayerSettings>();
    if (directory)
    {
        const std::filesystem::path file = *directory / "settings.dvx";
        std::error_code error;
        if (std::filesystem::exists(file, error))
        {
            if (const core::Result<std::string> text = core::readTextFile(file))
            {
                m_settings->read(*text);
            }
            else
            {
                DEVEX_LOG_WARNING("Cannot read the settings of the player: {}", text.error());
            }
        }
    }
    applyPlayerSettings();
    m_application.m_saves = m_saves.get();
    m_application.m_settings = m_settings.get();
}

void ApplicationRunner::destroyGameData()
{
    m_application.m_saves = nullptr;
    m_application.m_settings = nullptr;
    // The editor previews sounds at the volumes of the project, not those of the last game.
    if (m_services.audio != nullptr && isEditor())
    {
        m_services.audio->setPlayerMasterVolume(1.0f);
        for (std::uint32_t group = 0; group < asset::audioGroupCount; ++group)
        {
            m_services.audio->setPlayerGroupVolume(group, 1.0f);
        }
    }
    m_saves.reset();
    m_settings.reset();
}

void ApplicationRunner::applyPlayerSettings()
{
    if (!m_settings)
    {
        return;
    }
    if (m_services.audio != nullptr)
    {
        m_services.audio->setPlayerMasterVolume(m_settings->volume(PlayerSettings::master));
        for (std::uint32_t group = 0; group < asset::audioGroupCount; ++group)
        {
            m_services.audio->setPlayerGroupVolume(group, 1.0f);
        }
        for (const auto& [name, volume] : m_settings->volumes())
        {
            if (const std::optional<std::uint32_t> group = m_services.audio->findGroup(name))
            {
                m_services.audio->setPlayerGroupVolume(*group, volume);
            }
        }
    }
    // The window of the editor stays as it is; vertical sync applies from the next launch.
    const std::optional<bool> fullscreen = m_settings->fullscreen();
    if (!isEditor() && fullscreen && *fullscreen != m_services.window.isFullscreen())
    {
        m_services.window.setFullscreen(*fullscreen);
    }
}

void ApplicationRunner::updateGameData()
{
    if (m_settings && m_settings->takeChanges())
    {
        applyPlayerSettings();
        if (const std::optional<std::filesystem::path> directory = userDirectory())
        {
            if (core::Result<void> written = core::writeTextFile(*directory / "settings.dvx", m_settings->write()); !written)
            {
                DEVEX_LOG_WARNING("Cannot save the settings of the player: {}", written.error());
            }
        }
    }
    if (!m_saves)
    {
        return;
    }
    for (std::filesystem::path& file : m_saves->takeThumbnailRequests())
    {
        if (m_services.renderer != nullptr)
        {
            m_thumbnailCaptures.emplace(m_services.renderer->requestCapture(320, 180), std::move(file));
        }
    }
    if (std::optional<SaveGames::Restore> restore = m_saves->takeRestore())
    {
        core::Result<scene::Scene> restored = scene::loadScene(restore->sceneText);
        if (!restored)
        {
            DEVEX_LOG_ERROR("Cannot restore the scene of save '{}': {}", restore->slot, restored.error());
            return;
        }
        // Start systems see where the scene comes from, to leave alone what the save brought back.
        m_saves->setRestoredSlot(restore->slot);
        replaceScene(std::move(*restored), restore->scene);
        if (m_saves)
        {
            m_saves->setRestoredSlot({});
        }
    }
}

void ApplicationRunner::writeThumbnails()
{
    if (m_services.renderer == nullptr || m_thumbnailCaptures.empty())
    {
        return;
    }
    for (render::CapturedImage& captured : m_services.renderer->takeCaptures())
    {
        const auto found = m_thumbnailCaptures.find(captured.request);
        if (found == m_thumbnailCaptures.end())
        {
            continue;
        }
        const asset::Image image{.width = captured.width, .height = captured.height, .rgba = std::move(captured.rgba)};
        const std::vector<std::byte> png = asset::encodePng(image);
        if (core::Result<void> written = core::writeFileAtomically(found->second, png); !written)
        {
            DEVEX_LOG_WARNING("Cannot save the picture of a save: {}", written.error());
        }
        m_thumbnailCaptures.erase(found);
    }
}

void ApplicationRunner::saveBindings()
{
    const std::optional<std::filesystem::path> file = m_actions ? bindingsFile() : std::nullopt;
    if (!file)
    {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(file->parent_path(), error);
    if (core::Result<void> written = core::writeTextFile(*file, m_actions->writeOverrides()); !written)
    {
        DEVEX_LOG_WARNING("Cannot save the key bindings of the player: {}", written.error());
    }
}

void ApplicationRunner::updateUi(std::chrono::nanoseconds frameTime)
{
    if (!m_ui)
    {
        return;
    }
    // The interface answers where the game is shown: the whole window in the player, the viewport
    // image in the editor.
    math::Vec2 size{static_cast<float>(m_services.window.pixelSize().width),
                    static_cast<float>(m_services.window.pixelSize().height)};
    // Far outside every canvas, so that nothing is hovered while the pointer is elsewhere.
    math::Vec2 pointer{-1.0e6f, -1.0e6f};
    bool pointerAvailable = false;
    if (isEditor())
    {
        const math::Extent2D viewport = m_services.tools->viewportPixels();
        if (viewport.width == 0 || viewport.height == 0)
        {
            return;
        }
        size = math::Vec2{static_cast<float>(viewport.width), static_cast<float>(viewport.height)};
        if (const std::optional<math::Vec2> inside = m_services.tools->viewportPointer())
        {
            pointer = *inside;
            pointerAvailable = true;
        }
    }
    else
    {
        const math::Extent2D points = m_services.window.size();
        const float scale = points.width > 0 ? size.x / static_cast<float>(points.width) : 1.0f;
        pointer = m_services.platform.input().mousePosition() * scale;
        pointerAvailable = true;
    }

    const platform::Input& input = m_services.platform.input();
    const bool playing = !isEditor() || m_playState == tools::PlayState::Playing;
    ui::UiInput uiInput{.pointer = pointer};
    if (playing && pointerAvailable)
    {
        uiInput.pointerDown = input.isMouseButtonDown(platform::MouseButton::Left);
        uiInput.pointerPressed = input.wasMouseButtonPressed(platform::MouseButton::Left);
        uiInput.pointerReleased = input.wasMouseButtonReleased(platform::MouseButton::Left);
        uiInput.pointerMoved = input.mouseDelta() != math::Vec2{0.0f};
    }
    if (playing && pointerAvailable)
    {
        uiInput.wheel = input.mouseWheel().y;
    }
    if (playing)
    {
        // One step per press: the arrows and the pad move the focus, and the stick does too once
        // it is pushed, not every frame it stays there.
        const math::Vec2 stick = input.gamepadLeftStick();
        const auto step = [&](platform::Key negative, platform::Key positive,
                              platform::GamepadButton padNegative, platform::GamepadButton padPositive,
                              float axis, float previous) {
            int amount = 0;
            amount -= input.wasKeyPressed(negative) || input.wasGamepadButtonPressed(padNegative) ? 1 : 0;
            amount += input.wasKeyPressed(positive) || input.wasGamepadButtonPressed(padPositive) ? 1 : 0;
            constexpr float pushed = 0.5f;
            if (axis <= -pushed && previous > -pushed)
            {
                --amount;
            }
            if (axis >= pushed && previous < pushed)
            {
                ++amount;
            }
            return std::clamp(amount, -1, 1);
        };
        uiInput.moveX = step(platform::Key::Left, platform::Key::Right, platform::GamepadButton::DpadLeft,
                             platform::GamepadButton::DpadRight, stick.x, m_uiStick.x);
        uiInput.moveY = step(platform::Key::Up, platform::Key::Down, platform::GamepadButton::DpadUp,
                             platform::GamepadButton::DpadDown, stick.y, m_uiStick.y);
        m_uiStick = stick;
        // While a field is being typed into, the space bar writes a space rather than pressing
        // the button that has the focus.
        const bool editing = m_ui->isEditing();
        uiInput.submitPressed =
            input.wasKeyPressed(platform::Key::Enter) ||
            input.wasKeyPressed(platform::Key::KeypadEnter) ||
            (!editing && (input.wasKeyPressed(platform::Key::Space) ||
                          input.wasGamepadButtonPressed(platform::GamepadButton::South)));
        uiInput.cancelPressed = input.wasKeyPressed(platform::Key::Escape) ||
                                input.wasGamepadButtonPressed(platform::GamepadButton::East);

        // What a field takes: the letters the system made of the keys, and the keys that move the
        // cursor, which answer the repeats of a key held down as well as its first press.
        uiInput.typed = input.typedText();
        const auto stroke = [&input](platform::Key key) {
            return input.wasKeyPressed(key) || input.wasKeyRepeated(key);
        };
        const auto held = [&input](platform::Key left, platform::Key right) {
            return input.isKeyDown(left) || input.isKeyDown(right);
        };
        const bool control = held(platform::Key::LeftControl, platform::Key::RightControl);
        uiInput.backspacePressed = stroke(platform::Key::Backspace);
        uiInput.deletePressed = stroke(platform::Key::Delete);
        uiInput.leftPressed = stroke(platform::Key::Left);
        uiInput.rightPressed = stroke(platform::Key::Right);
        uiInput.upPressed = stroke(platform::Key::Up);
        uiInput.downPressed = stroke(platform::Key::Down);
        uiInput.homePressed = stroke(platform::Key::Home);
        uiInput.endPressed = stroke(platform::Key::End);
        uiInput.selecting = held(platform::Key::LeftShift, platform::Key::RightShift);
        // The shortcuts follow the letters of the layout rather than the places of the keys.
        uiInput.copyPressed = control && input.wasLetterPressed('c');
        uiInput.cutPressed = control && input.wasLetterPressed('x');
        uiInput.pastePressed = control && input.wasLetterPressed('v');
        uiInput.selectAllPressed = control && input.wasLetterPressed('a');
        if (uiInput.pastePressed)
        {
            uiInput.clipboard = m_services.platform.clipboardText();
        }
    }
    // The key or the button that answered a binding the player was choosing does nothing else.
    if (m_actions && m_actions->tookInput())
    {
        uiInput.pointerPressed = false;
        uiInput.submitPressed = false;
        uiInput.cancelPressed = false;
        uiInput.moveX = 0;
        uiInput.moveY = 0;
    }
    m_ui->update(*m_application.m_scene, size, uiInput, core::Duration(frameTime));

    // The system types into the field that is being edited: its keyboard opens on a touch screen,
    // and a dead key composes into one letter.
    if (m_ui->isEditing() != m_services.platform.isTextInputActive())
    {
        m_services.platform.setTextInput(m_services.window, m_ui->isEditing());
    }
    if (const std::string& copied = m_ui->clipboardRequest(); !copied.empty())
    {
        m_services.platform.setClipboardText(copied);
    }
}

void ApplicationRunner::buildUi(render::RenderWorld& world)
{
    if (!m_ui)
    {
        return;
    }
    ui::DrawContext context{
        .fonts =
            [this](asset::AssetId id) {
                const LoadedFont* const font = m_services.assets.font(id);
                return font != nullptr ? ui::FontRef{.data = font->data.get(), .atlas = font->atlas}
                                       : ui::FontRef{};
            },
        .textures = [this](asset::AssetId id) { return m_services.assets.texture(id); },
        .textureSize =
            [this](asset::AssetId id) {
                const math::Extent2D size = m_services.assets.textureSize(id);
                return math::Vec2{static_cast<float>(size.width), static_cast<float>(size.height)};
            },
    };
    m_ui->build(*m_application.m_scene, context, world);
}

void ApplicationRunner::destroyAnimation()
{
    if (m_services.tools != nullptr)
    {
        m_services.tools->setAnimationWorld(nullptr);
    }
    m_application.m_animation = nullptr;
    m_animation.reset();
}

void ApplicationRunner::updateAnimation(std::chrono::nanoseconds frameTime)
{
    if (!m_animation)
    {
        return;
    }
    const bool paused = isEditor() && m_playState != tools::PlayState::Playing;
    m_animation->setPaused(paused);
    m_animation->update(*m_application.m_scene, core::Duration(frameTime));
}

void ApplicationRunner::applyAudioSettings()
{
    if (m_services.audio == nullptr)
    {
        return;
    }
    const asset::AssetSource* const source = assetSource();
    asset::AudioSettings settings = source != nullptr ? source->project().audio : asset::AudioSettings{};
    if (settings != m_audioSettings)
    {
        m_services.audio->configure(settings);
        m_audioSettings = std::move(settings);
    }
}

void ApplicationRunner::updateAudio(std::chrono::nanoseconds frameTime)
{
    applyAudioSettings();
    if (!m_audio)
    {
        return;
    }
    const bool paused = isEditor() && m_playState != tools::PlayState::Playing;
    m_audio->setPaused(paused);
    if (!paused)
    {
        m_audio->update(*m_application.m_scene, core::Duration(frameTime));
    }
}

std::vector<tools::CodeDiagnostic> ApplicationRunner::codeDiagnostics() const
{
    std::vector<tools::CodeDiagnostic> diagnostics;
    const auto collect = [&diagnostics](const auto& builder) {
        for (const GameCodeBuilder::Diagnostic& reported : builder.diagnostics())
        {
            diagnostics.push_back({
                .path = reported.path,
                .line = reported.line,
                .column = reported.column,
                .message = reported.message,
                .error = reported.error,
            });
        }
    };
    if (m_gameBuilder)
    {
        collect(*m_gameBuilder);
    }
    if (m_managedBuilder)
    {
        collect(*m_managedBuilder);
    }
    // MSBuild prints each diagnostic twice: once where it happens, once in its summary.
    std::vector<tools::CodeDiagnostic> unique;
    for (tools::CodeDiagnostic& diagnostic : diagnostics)
    {
        const bool seen = std::ranges::any_of(unique, [&diagnostic](const tools::CodeDiagnostic& other) {
            return other.line == diagnostic.line && other.column == diagnostic.column &&
                   other.path == diagnostic.path && other.message == diagnostic.message;
        });
        if (!seen)
        {
            unique.push_back(std::move(diagnostic));
        }
    }
    return unique;
}

tools::GameCodeStatus ApplicationRunner::gameCodeStatus() const
{
    using State = tools::GameCodeStatus::State;
    const asset::Project* const project = m_services.database != nullptr ? &m_services.database->project() : nullptr;
    const bool cpp = project != nullptr && GameCodeBuilder::hasCode(*project);
    const bool csharp = project != nullptr && ManagedCodeBuilder::hasCode(*project);
    if (!cpp && !csharp)
    {
        return {.state = State::None};
    }
    if ((m_gameBuilder && m_gameBuilder->pending()) ||
        (m_managedBuilder && m_managedBuilder->state() == ManagedCodeBuilder::State::Building))
    {
        return {.state = State::Building, .message = "Compiling the game code..."};
    }
    if (m_gameBuilder && m_gameBuilder->state() == GameCodeBuilder::State::Failed)
    {
        return {.state = State::Failed, .message = m_gameBuilder->message()};
    }
    if (m_managedBuilder && m_managedBuilder->state() == ManagedCodeBuilder::State::Failed)
    {
        return {.state = State::Failed, .message = m_managedBuilder->message()};
    }
    if (cpp && !m_game)
    {
        return {.state = State::Failed, .message = m_gameLoadError.empty() ? "The game code is not loaded" : m_gameLoadError};
    }
    if (csharp && (!m_managed || !m_managed->hasAssembly()))
    {
        return {.state = State::Failed, .message = "The C# code is not loaded"};
    }
    std::string message;
    if (m_game)
    {
        message = std::format("{} components, {} systems", m_game->registry().components().size(),
                              m_game->registry().systems().size());
    }
    if (m_managed && m_managed->hasAssembly())
    {
        message += std::format("{}{} C# components", message.empty() ? "" : ", ", m_managed->componentTypes().size());
    }
    return {.state = State::Ready, .message = std::move(message)};
}

void ApplicationRunner::createScript(const tools::NewScript& script)
{
    const asset::Project& project = m_services.database->project();
    const core::Result<std::filesystem::path> file =
        script.csharp ? ManagedCodeBuilder::createScript(project, script.name)
                      : GameCodeBuilder::createComponent(project, script.name);
    if (!file)
    {
        DEVEX_LOG_ERROR("Cannot create {}: {}", script.name, file.error());
        return;
    }
    DEVEX_LOG_INFO("Created {}", core::toUtf8(*file));
    if (!script.csharp)
    {
        DEVEX_LOG_INFO("Add game.component<{}>(); to the module of the game to use it", script.name);
    }
    else if (!m_managedBuilder)
    {
        // The first C# file of a project starts its builder.
        openManagedCode();
    }
    m_services.tools->openTextFile(*file);
}

void ApplicationRunner::updateExport()
{
    if (m_exportWaiting && m_services.database != nullptr)
    {
        const bool building = m_gameBuilder && m_gameBuilder->state() == GameCodeBuilder::State::Building;
        if (m_services.database->pendingImports() > 0 || building)
        {
            m_services.tools->setExportStatus(
                {.state = tools::ExportStatus::State::Running, .message = "Waiting for imports and builds to finish"});
        }
        else
        {
            m_exportWaiting = false;
            startExport();
        }
    }
    if (m_export)
    {
        m_services.tools->setExportStatus(m_export->status());
        if (m_export->finished())
        {
            m_export.reset();
        }
    }
}

void ApplicationRunner::startExport()
{
    const asset::Project& project = m_services.database->project();
    const std::vector<EngineBuild> builds = findEngineBuilds(m_services.platform.baseDirectory());
    const std::optional<EngineBuild> engine = findEngineBuild(builds, project.exportSettings.configuration);
    if (!engine)
    {
        const std::string message =
            std::format("there is no {} build of the engine next to the editor", project.exportSettings.configuration);
        DEVEX_LOG_ERROR("Cannot export {}: {}", project.name, message);
        m_services.tools->setExportStatus({.state = tools::ExportStatus::State::Failed, .message = message});
        return;
    }
    core::Result<ExportPlan> plan = planExport(*m_services.database, *engine);
    if (!plan)
    {
        DEVEX_LOG_ERROR("Cannot export {}: {}", project.name, plan.error());
        m_services.tools->setExportStatus({.state = tools::ExportStatus::State::Failed, .message = plan.error().message});
        return;
    }
    DEVEX_LOG_INFO("Exporting {} with the {} engine build to {}", project.name, engine->configuration,
                   core::toUtf8(plan->output));
    m_export = std::make_unique<ExportJob>(std::move(*plan));
    m_services.tools->setExportStatus(m_export->status());
}

// Uploads the built-in meshes and registers them under their reserved asset identifiers.
[[nodiscard]] core::Result<void> registerBuiltinMeshes(render::Renderer& renderer,
                                                       AssetManager& assets)
{
    for (const asset::AssetId id : {asset::builtin::cubeMesh, asset::builtin::sphereMesh, asset::builtin::planeMesh})
    {
        core::Result<render::MeshHandle> handle = renderer.createMesh(*asset::makeBuiltinMesh(id));
        if (!handle)
        {
            return std::unexpected(handle.error());
        }
        assets.registerMesh(id, *handle);
    }
    return {};
}

} // namespace detail

const platform::Platform& Application::platform() const noexcept
{
    DEVEX_ASSERT_MSG(m_platform != nullptr, "engine services are unavailable outside run()");
    return *m_platform;
}

const platform::Input& Application::input() const noexcept
{
    return platform().input();
}

platform::Window& Application::window() noexcept
{
    DEVEX_ASSERT_MSG(m_window != nullptr, "engine services are unavailable outside run()");
    return *m_window;
}

scene::Scene& Application::scene() noexcept
{
    DEVEX_ASSERT_MSG(m_scene != nullptr, "engine services are unavailable outside run()");
    return *m_scene;
}

AssetManager& Application::assets() noexcept
{
    DEVEX_ASSERT_MSG(m_assets != nullptr, "engine services are unavailable outside run()");
    return *m_assets;
}

asset::AssetDatabase* Application::assetDatabase() noexcept
{
    DEVEX_ASSERT_MSG(m_runner != nullptr, "engine services are unavailable outside run()");
    return m_runner->database();
}

asset::AssetSource* Application::assetSource() noexcept
{
    DEVEX_ASSERT_MSG(m_runner != nullptr, "engine services are unavailable outside run()");
    return m_runner->assetSource();
}

const asset::Project* Application::project() noexcept
{
    const asset::AssetSource* const source = assetSource();
    return source != nullptr ? &source->project() : nullptr;
}

core::Result<void> Application::loadScene(asset::AssetId sceneAsset)
{
    DEVEX_ASSERT_MSG(m_runner != nullptr, "engine services are unavailable outside run()");
    return m_runner->loadScene(sceneAsset);
}

void Application::loadSceneInBackground(asset::AssetId sceneAsset)
{
    DEVEX_ASSERT_MSG(m_runner != nullptr, "engine services are unavailable outside run()");
    m_runner->loadSceneInBackground(sceneAsset);
}

std::optional<float> Application::sceneLoadingProgress() const noexcept
{
    return m_runner != nullptr ? m_runner->sceneLoadingProgress() : std::nullopt;
}

core::JobSystem& Application::jobs() noexcept
{
    DEVEX_ASSERT_MSG(m_jobs != nullptr, "engine services are unavailable outside run()");
    return *m_jobs;
}

render::Renderer& Application::renderer() noexcept
{
    DEVEX_ASSERT_MSG(m_renderer != nullptr, "rendering is disabled or unavailable outside run()");
    return *m_renderer;
}

const render::Renderer& Application::renderer() const noexcept
{
    DEVEX_ASSERT_MSG(m_renderer != nullptr, "rendering is disabled or unavailable outside run()");
    return *m_renderer;
}

double Application::interpolationAlpha() const noexcept
{
    return m_interpolationAlpha;
}

physics::PhysicsWorld* Application::physics() noexcept
{
    return m_physics;
}

audio::AudioWorld* Application::audio() noexcept
{
    return m_audio;
}

animation::AnimationWorld* Application::animation() noexcept
{
    return m_animation;
}

ui::UiWorld* Application::ui() noexcept
{
    return m_ui;
}

InputActions* Application::inputActions() noexcept
{
    return m_actions;
}

SaveGames* Application::saves() noexcept
{
    return m_saves;
}

PlayerSettings* Application::playerSettings() noexcept
{
    return m_settings;
}

bool Application::isEditor() const noexcept
{
    return m_editor;
}

bool Application::isPlaying() const noexcept
{
    return m_playing;
}

void Application::requestQuit() noexcept
{
    if (m_editor)
    {
        m_stopRequested = true;
        return;
    }
    m_quitRequested = true;
}

int run(Application& application, const ApplicationConfig& config)
{
    DEVEX_LOG_INFO("Devex Engine {}", core::version());
    if (config.editor && !config.enableRendering)
    {
        DEVEX_LOG_FATAL("The editor needs rendering");
        return EXIT_FAILURE;
    }

    core::Result<platform::Platform> platform = platform::Platform::create();
    if (!platform)
    {
        DEVEX_LOG_FATAL("Cannot initialize the platform: {}", platform.error());
        return EXIT_FAILURE;
    }

    // An exported game comes with its package, whose settings open the window.
    std::unique_ptr<asset::PackageReader> package;
    std::optional<asset::Project> launchSettings;
    if (!config.package.empty())
    {
        const std::filesystem::path packagePath =
            config.package.is_absolute() ? config.package : platform->baseDirectory() / config.package;
        core::Result<std::unique_ptr<asset::PackageReader>> opened = asset::PackageReader::open(packagePath);
        if (!opened)
        {
            DEVEX_LOG_FATAL("Cannot open the game: {}", opened.error());
            return EXIT_FAILURE;
        }
        package = std::move(*opened);
        launchSettings = package->project();
        DEVEX_LOG_INFO("Game {}: {} assets", launchSettings->name, package->assetCount());
    }
    else if (!config.project.empty())
    {
        if (core::Result<asset::Project> project = asset::loadProject(config.project))
        {
            launchSettings = std::move(*project);
        }
    }
    ApplicationConfig effective = config;
    bool fullscreen = false;
    if (config.useProjectWindowSettings && launchSettings)
    {
        const asset::WindowSettings& settings = launchSettings->window;
        effective.title = launchSettings->name;
        effective.width = settings.width;
        effective.height = settings.height;
        effective.maxFrameRate = settings.maxFrameRate;
        fullscreen = settings.fullscreen;
        bool vsync = settings.vsync;
        // What the player chose last time comes before what the project says.
        const core::Result<std::filesystem::path> user =
            config.userDirectory.empty() ? platform::userDataLocation("", launchSettings->name)
                                         : core::Result<std::filesystem::path>(config.userDirectory);
        std::error_code error;
        if (user && std::filesystem::exists(*user / "settings.dvx", error))
        {
            if (const core::Result<std::string> text = core::readTextFile(*user / "settings.dvx"))
            {
                PlayerSettings chosen;
                chosen.read(*text);
                fullscreen = chosen.fullscreen().value_or(fullscreen);
                vsync = chosen.vsync().value_or(vsync);
            }
        }
        effective.presentMode = vsync ? render::PresentMode::Fifo : render::PresentMode::Immediate;
    }

    core::Result<platform::Window> window = platform->createWindow({
        .title = effective.title,
        .width = effective.width,
        .height = effective.height,
        .resizable = effective.resizable,
        .vulkan = effective.enableRendering,
        .fullscreen = fullscreen,
    });
    if (!window)
    {
        DEVEX_LOG_FATAL("Cannot create the main window: {}", window.error());
        return EXIT_FAILURE;
    }

    const math::Extent2D pixelSize = window->pixelSize();
    DEVEX_LOG_DEBUG("Main window {}x{} ({}x{} pixels), fixed update at {} Hz", effective.width,
                    effective.height, pixelSize.width, pixelSize.height, config.fixedUpdateRate);

    // Destroyed last: running imports finish before the process exits.
    core::JobSystem jobs(config.workerThreads);

    std::unique_ptr<asset::AssetDatabase> database;
    if (!config.project.empty())
    {
        core::Result<std::unique_ptr<asset::AssetDatabase>> opened =
            detail::openProject(config.project, jobs, config.watchAssets);
        if (!opened)
        {
            DEVEX_LOG_FATAL("Cannot open the project: {}", opened.error());
            return EXIT_FAILURE;
        }
        database = std::move(*opened);
    }
    if (config.useProjectWindowSettings)
    {
        detail::applyWindowIcon(*window, database.get(), package.get());
    }

    std::optional<render::Renderer> renderer;
    if (config.enableRendering)
    {
        core::Result<render::Renderer> created = render::Renderer::create(*platform, *window, {
            .applicationName = effective.title,
            .presentMode = effective.presentMode,
            .preferredGpu = config.preferredGpu,
        });
        if (!created)
        {
            DEVEX_LOG_FATAL("Cannot initialize rendering: {}", created.error());
            return EXIT_FAILURE;
        }
        renderer.emplace(std::move(*created));
    }

    // Sounds that play stop before the mixer goes: the runner, which owns them, is destroyed first.
    std::unique_ptr<audio::AudioEngine> audioEngine;
    if (config.enableAudio)
    {
        core::Result<std::unique_ptr<audio::AudioEngine>> created =
            audio::AudioEngine::create({.device = config.audioOutput});
        if (created)
        {
            audioEngine = std::move(*created);
        }
        else
        {
            DEVEX_LOG_ERROR("The game runs without sound: {}", created.error());
        }
    }

    // Meshes and textures load on the workers of the job system.
    AssetManager assets(renderer ? &*renderer : nullptr,
                        database != nullptr ? static_cast<asset::AssetSource*>(database.get()) : package.get(), &jobs);
    if (renderer)
    {
        if (core::Result<void> builtins = detail::registerBuiltinMeshes(*renderer, assets);
            !builtins)
        {
            DEVEX_LOG_FATAL("Cannot upload the built-in meshes: {}", builtins.error());
            return EXIT_FAILURE;
        }
    }

    // Destroyed before the renderer and the platform it is connected to.
    std::unique_ptr<tools::ToolsOverlay> tools;
    if ((config.enableTools || config.editor) && renderer)
    {
        const tools::ToolsMode mode = config.editor ? tools::ToolsMode::Editor : tools::ToolsMode::Overlay;
        core::Result<std::unique_ptr<tools::ToolsOverlay>> overlay = tools::ToolsOverlay::create(
            *platform, *window, *renderer,
            platform->baseDirectory() / (config.editor ? "devex-editor.ini" : "devex-tools.ini"), mode);
        if (overlay)
        {
            tools = std::move(*overlay);
            tools->setAssetDatabase(database.get());
            tools->setAudio(audioEngine.get(),
                            [&assets](asset::AssetId clip) { return assets.audioClip(clip); });
            tools->setAnimationClips([&assets](asset::AssetId clip) { return assets.animationClip(clip); });
            tools->setThemes([&assets](asset::AssetId theme) { return assets.theme(theme); });
            tools->setMemoryReport([&assets] { return assets.memoryReport(); });
            tools->setPendingLoads([&assets] { return assets.pendingLoads(); });
            const std::filesystem::path engineConfig = (platform->baseDirectory() / ".." / "cmake").lexically_normal();
            tools->setProjectCodeStatusProvider([engineConfig](const asset::Project& project) {
                const auto status = detail::GameCodeBuilder::buildStatus(project, engineConfig);
                return tools::ProjectCodeStatus{.needsUpdate = status.needsBuild, .message = status.message};
            });
            if (!config.editor)
            {
                DEVEX_LOG_INFO("Press F1 to show the tools");
            }
        }
        else if (config.editor)
        {
            DEVEX_LOG_FATAL("Cannot start the editor: {}", overlay.error());
            return EXIT_FAILURE;
        }
        else
        {
            DEVEX_LOG_WARNING("Tools are unavailable: {}", overlay.error());
        }
    }

    scene::Scene scene;
    detail::ApplicationRunner runner(application, effective,
                                     {
                                         .platform = *platform,
                                         .window = *window,
                                         .renderer = renderer ? &*renderer : nullptr,
                                         .scene = scene,
                                         .assets = assets,
                                         .jobs = jobs,
                                         .tools = tools.get(),
                                         .audio = audioEngine.get(),
                                         .database = database,
                                         .package = package,
                                     });
    return runner.execute();
}

} // namespace devex::runtime
