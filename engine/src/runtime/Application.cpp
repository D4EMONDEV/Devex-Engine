#include <devex/asset/AssetId.hpp>
#include <devex/asset/Primitives.hpp>
#include <devex/core/Assert.hpp>
#include <devex/core/BuildInfo.hpp>
#include <devex/core/Log.hpp>
#include <devex/runtime/Application.hpp>
#include <devex/runtime/FixedTimestep.hpp>
#include <devex/runtime/SceneExtraction.hpp>
#include <devex/tools/ToolsOverlay.hpp>

#include <algorithm>
#include <memory>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <variant>

namespace devex::runtime {

namespace detail {

struct EngineServices
{
    platform::Platform& platform;
    platform::Window& window;
    render::Renderer* renderer = nullptr;
    scene::Scene& scene;
    AssetRegistry& assets;
    tools::ToolsOverlay* tools = nullptr;
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

private:
    using Clock = std::chrono::steady_clock;

    // A minimized window shows nothing: keep simulating in real time without spinning the CPU.
    static constexpr std::chrono::milliseconds minimizedFrameTime{50};

    void handleEvent(const platform::Event& event);
    // Runs the updates and the rendering of one frame. Also called from the operating system's
    // modal loop while the window is being resized, where events cannot be polled.
    void runFrame();

    Application& m_application;
    EngineServices m_services;
    platform::WindowId m_mainWindow;
    FixedTimestep m_timestep;
    core::Duration m_fixedDelta;
    std::chrono::nanoseconds m_frameBudget;
    Clock::time_point m_previousFrame;
    bool m_inFrame = false;
    int m_exitCode = EXIT_SUCCESS;
};

ApplicationRunner::ApplicationRunner(Application& application, const ApplicationConfig& config,
                                     const EngineServices& services) noexcept
    : m_application(application)
    , m_services(services)
    , m_mainWindow(services.window.id())
    , m_timestep(FixedTimestep::fromRate(config.fixedUpdateRate))
    , m_fixedDelta(m_timestep.step())
    , m_frameBudget(config.maxFrameRate == 0
                        ? std::chrono::nanoseconds::zero()
                        : std::chrono::nanoseconds(std::chrono::seconds(1)) / config.maxFrameRate)
{
    m_application.m_platform = &services.platform;
    m_application.m_window = &services.window;
    m_application.m_renderer = services.renderer;
    m_application.m_scene = &services.scene;
    m_application.m_assets = &services.assets;
    m_application.m_interpolationAlpha = 0.0;
    m_application.m_quitRequested = false;
}

ApplicationRunner::~ApplicationRunner()
{
    m_services.platform.setLiveRedrawCallback({});
    m_application.m_platform = nullptr;
    m_application.m_window = nullptr;
    m_application.m_renderer = nullptr;
    m_application.m_scene = nullptr;
    m_application.m_assets = nullptr;
}

int ApplicationRunner::execute()
{
    if (core::Result<void> started = m_application.onStartup(); !started)
    {
        DEVEX_LOG_FATAL("Application startup failed: {}", started.error());
        return EXIT_FAILURE;
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
        // Devices used by the tools during the previous frame do not drive gameplay.
        if (m_services.tools != nullptr)
        {
            m_services.platform.setImGuiInputCapture(m_services.tools->capturesKeyboard(),
                                                     m_services.tools->capturesMouse());
        }
        m_services.platform.pollEvents(
            [this](const platform::Event& event) { handleEvent(event); });
        if (m_application.m_quitRequested)
        {
            break;
        }
        runFrame();
    }

    m_services.platform.setLiveRedrawCallback({});
    m_application.onShutdown();
    return m_exitCode;
}

void ApplicationRunner::handleEvent(const platform::Event& event)
{
    m_application.onEvent(event);

    if (const auto* key = std::get_if<platform::KeyPressed>(&event);
        key != nullptr && key->key == platform::Key::F1 && !key->repeat &&
        m_services.tools != nullptr)
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
        m_application.m_quitRequested = true;
    }
    else if (const auto* closeRequest = std::get_if<platform::WindowCloseRequested>(&event);
             closeRequest != nullptr && closeRequest->window == m_mainWindow)
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

    const std::uint32_t steps = m_timestep.advance(frameTime);
    for (std::uint32_t step = 0; step < steps; ++step)
    {
        m_application.onFixedUpdate(m_fixedDelta);
    }
    m_application.m_interpolationAlpha = m_timestep.alpha();
    m_application.onUpdate(core::Duration(frameTime));

    // The application may have replaced the scene during the updates.
    scene::Scene& scene = *m_application.m_scene;
    scene.updateTransforms();

    const bool minimized = m_services.window.isMinimized();
    render::Renderer* const renderer = m_services.renderer;
    if (renderer != nullptr && !minimized && !m_application.m_quitRequested)
    {
        if (m_services.tools != nullptr)
        {
            m_services.tools->update(scene, core::Duration(frameTime));
        }
        render::RenderWorld& world = renderer->beginFrame();
        extractScene(scene, m_services.assets, world);
        m_application.onRender(world);
        if (core::Result<void> rendered = renderer->endFrame(); !rendered)
        {
            DEVEX_LOG_FATAL("Rendering failed: {}", rendered.error());
            m_application.m_quitRequested = true;
            m_exitCode = EXIT_FAILURE;
        }
    }

    const std::chrono::nanoseconds frameLimit =
        minimized ? std::max(m_frameBudget, std::chrono::nanoseconds(minimizedFrameTime))
                  : m_frameBudget;
    if (frameLimit > std::chrono::nanoseconds::zero())
    {
        platform::sleepPrecise(frameLimit - (Clock::now() - frameStart));
    }

    m_inFrame = false;
}

// Uploads the built-in meshes and registers them under their reserved asset identifiers.
[[nodiscard]] core::Result<void> registerBuiltinMeshes(render::Renderer& renderer,
                                                       AssetRegistry& assets)
{
    const std::pair<asset::AssetId, asset::MeshData> builtins[] = {
        {asset::builtin::cubeMesh, asset::makeCube()},
        {asset::builtin::sphereMesh, asset::makeUvSphere(0.5f, 48, 24)},
        {asset::builtin::planeMesh, asset::makePlane()},
    };
    for (const auto& [id, mesh] : builtins)
    {
        core::Result<render::MeshHandle> handle = renderer.createMesh(mesh);
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

AssetRegistry& Application::assets() noexcept
{
    DEVEX_ASSERT_MSG(m_assets != nullptr, "engine services are unavailable outside run()");
    return *m_assets;
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

void Application::requestQuit() noexcept
{
    m_quitRequested = true;
}

int run(Application& application, const ApplicationConfig& config)
{
    DEVEX_LOG_INFO("Devex Engine {}", core::version());

    core::Result<platform::Platform> platform = platform::Platform::create();
    if (!platform)
    {
        DEVEX_LOG_FATAL("Cannot initialize the platform: {}", platform.error());
        return EXIT_FAILURE;
    }

    core::Result<platform::Window> window = platform->createWindow({
        .title = config.title,
        .width = config.width,
        .height = config.height,
        .resizable = config.resizable,
        .vulkan = config.enableRendering,
    });
    if (!window)
    {
        DEVEX_LOG_FATAL("Cannot create the main window: {}", window.error());
        return EXIT_FAILURE;
    }

    const math::Extent2D pixelSize = window->pixelSize();
    DEVEX_LOG_DEBUG("Main window {}x{} ({}x{} pixels), fixed update at {} Hz", config.width,
                    config.height, pixelSize.width, pixelSize.height, config.fixedUpdateRate);

    AssetRegistry assets;
    std::optional<render::Renderer> renderer;
    if (config.enableRendering)
    {
        core::Result<render::Renderer> created = render::Renderer::create(*platform, *window, {
            .applicationName = config.title,
            .presentMode = config.presentMode,
            .preferredGpu = config.preferredGpu,
        });
        if (!created)
        {
            DEVEX_LOG_FATAL("Cannot initialize rendering: {}", created.error());
            return EXIT_FAILURE;
        }
        renderer.emplace(std::move(*created));

        if (core::Result<void> builtins = detail::registerBuiltinMeshes(*renderer, assets);
            !builtins)
        {
            DEVEX_LOG_FATAL("Cannot upload the built-in meshes: {}", builtins.error());
            return EXIT_FAILURE;
        }
    }

    // Destroyed before the renderer and the platform it is connected to.
    std::unique_ptr<tools::ToolsOverlay> tools;
    if (config.enableTools && renderer)
    {
        core::Result<std::unique_ptr<tools::ToolsOverlay>> overlay = tools::ToolsOverlay::create(
            *platform, *window, *renderer, platform->baseDirectory() / "devex-tools.ini");
        if (overlay)
        {
            tools = std::move(*overlay);
            DEVEX_LOG_INFO("Press F1 to show the tools");
        }
        else
        {
            DEVEX_LOG_WARNING("Tools are unavailable: {}", overlay.error());
        }
    }

    scene::Scene scene;
    detail::ApplicationRunner runner(application, config,
                                     {
                                         .platform = *platform,
                                         .window = *window,
                                         .renderer = renderer ? &*renderer : nullptr,
                                         .scene = scene,
                                         .assets = assets,
                                         .tools = tools.get(),
                                     });
    return runner.execute();
}

} // namespace devex::runtime
