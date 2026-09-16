#include <devex/core/Assert.hpp>
#include <devex/core/BuildInfo.hpp>
#include <devex/core/Log.hpp>
#include <devex/runtime/Application.hpp>
#include <devex/runtime/FixedTimestep.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <variant>

namespace devex::runtime {

namespace detail {

class ApplicationRunner
{
public:
    ApplicationRunner(Application& application, const ApplicationConfig& config,
                      platform::Platform& platform, platform::Window& window,
                      render::Renderer* renderer) noexcept;
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
    platform::Platform& m_platform;
    platform::Window& m_window;
    render::Renderer* m_renderer;
    platform::WindowId m_mainWindow;
    FixedTimestep m_timestep;
    core::Duration m_fixedDelta;
    std::chrono::nanoseconds m_frameBudget;
    Clock::time_point m_previousFrame;
    bool m_inFrame = false;
    int m_exitCode = EXIT_SUCCESS;
};

ApplicationRunner::ApplicationRunner(Application& application, const ApplicationConfig& config,
                                     platform::Platform& platform, platform::Window& window,
                                     render::Renderer* renderer) noexcept
    : m_application(application)
    , m_platform(platform)
    , m_window(window)
    , m_renderer(renderer)
    , m_mainWindow(window.id())
    , m_timestep(FixedTimestep::fromRate(config.fixedUpdateRate))
    , m_fixedDelta(m_timestep.step())
    , m_frameBudget(config.maxFrameRate == 0
                        ? std::chrono::nanoseconds::zero()
                        : std::chrono::nanoseconds(std::chrono::seconds(1)) / config.maxFrameRate)
{
    m_application.m_platform = &platform;
    m_application.m_window = &window;
    m_application.m_renderer = renderer;
    m_application.m_interpolationAlpha = 0.0;
    m_application.m_quitRequested = false;
}

ApplicationRunner::~ApplicationRunner()
{
    m_platform.setLiveRedrawCallback({});
    m_application.m_platform = nullptr;
    m_application.m_window = nullptr;
    m_application.m_renderer = nullptr;
}

int ApplicationRunner::execute()
{
    if (core::Result<void> started = m_application.onStartup(); !started)
    {
        DEVEX_LOG_FATAL("Application startup failed: {}", started.error());
        return EXIT_FAILURE;
    }

    m_platform.setLiveRedrawCallback([this] {
        if (!m_inFrame && !m_application.m_quitRequested)
        {
            runFrame();
        }
    });

    m_previousFrame = Clock::now();
    while (!m_application.m_quitRequested)
    {
        m_platform.pollEvents([this](const platform::Event& event) { handleEvent(event); });
        if (m_application.m_quitRequested)
        {
            break;
        }
        runFrame();
    }

    m_platform.setLiveRedrawCallback({});
    m_application.onShutdown();
    return m_exitCode;
}

void ApplicationRunner::handleEvent(const platform::Event& event)
{
    m_application.onEvent(event);

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

    const bool minimized = m_window.isMinimized();
    if (m_renderer != nullptr && !minimized && !m_application.m_quitRequested)
    {
        m_application.onRender(m_renderer->beginFrame());
        if (core::Result<void> rendered = m_renderer->endFrame(); !rendered)
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
    }

    detail::ApplicationRunner runner(application, config, *platform, *window,
                                     renderer ? &*renderer : nullptr);
    return runner.execute();
}

} // namespace devex::runtime
