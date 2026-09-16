#include <devex/core/Assert.hpp>
#include <devex/core/BuildInfo.hpp>
#include <devex/core/Log.hpp>
#include <devex/runtime/Application.hpp>
#include <devex/runtime/FixedTimestep.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <variant>

namespace devex::runtime {

namespace detail {

class ApplicationRunner
{
public:
    ApplicationRunner(Application& application, platform::Platform& platform,
                      platform::Window& window) noexcept
        : m_application(application)
        , m_platform(platform)
        , m_window(window)
    {
        m_application.m_platform = &platform;
        m_application.m_window = &window;
        m_application.m_interpolationAlpha = 0.0;
        m_application.m_quitRequested = false;
    }

    ~ApplicationRunner()
    {
        m_application.m_platform = nullptr;
        m_application.m_window = nullptr;
    }

    ApplicationRunner(const ApplicationRunner&) = delete;
    ApplicationRunner& operator=(const ApplicationRunner&) = delete;

    [[nodiscard]] int execute(const ApplicationConfig& config);

private:
    using Clock = std::chrono::steady_clock;

    // A minimized window shows nothing: keep simulating in real time without spinning the CPU.
    static constexpr std::chrono::milliseconds minimizedFrameTime{50};

    void handleEvent(const platform::Event& event, platform::WindowId mainWindow);

    Application& m_application;
    platform::Platform& m_platform;
    platform::Window& m_window;
};

int ApplicationRunner::execute(const ApplicationConfig& config)
{
    if (core::Result<void> started = m_application.onStartup(); !started)
    {
        DEVEX_LOG_FATAL("Application startup failed: {}", started.error());
        return EXIT_FAILURE;
    }

    FixedTimestep timestep = FixedTimestep::fromRate(config.fixedUpdateRate);
    const core::Duration fixedDelta = timestep.step();
    const std::chrono::nanoseconds frameBudget =
        config.maxFrameRate == 0 ? std::chrono::nanoseconds::zero()
                                 : std::chrono::nanoseconds(std::chrono::seconds(1)) /
                                       config.maxFrameRate;
    const platform::WindowId mainWindow = m_window.id();

    Clock::time_point previousFrame = Clock::now();
    while (!m_application.m_quitRequested)
    {
        const Clock::time_point frameStart = Clock::now();
        const std::chrono::nanoseconds frameTime = frameStart - previousFrame;
        previousFrame = frameStart;

        m_platform.pollEvents(
            [this, mainWindow](const platform::Event& event) { handleEvent(event, mainWindow); });
        if (m_application.m_quitRequested)
        {
            break;
        }

        const std::uint32_t steps = timestep.advance(frameTime);
        for (std::uint32_t step = 0; step < steps; ++step)
        {
            m_application.onFixedUpdate(fixedDelta);
        }
        m_application.m_interpolationAlpha = timestep.alpha();
        m_application.onUpdate(core::Duration(frameTime));

        const std::chrono::nanoseconds frameLimit =
            m_window.isMinimized()
                ? std::max(frameBudget, std::chrono::nanoseconds(minimizedFrameTime))
                : frameBudget;
        if (frameLimit > std::chrono::nanoseconds::zero())
        {
            platform::sleepPrecise(frameLimit - (Clock::now() - frameStart));
        }
    }

    m_application.onShutdown();
    return EXIT_SUCCESS;
}

void ApplicationRunner::handleEvent(const platform::Event& event, platform::WindowId mainWindow)
{
    m_application.onEvent(event);

    if (std::holds_alternative<platform::QuitRequested>(event))
    {
        m_application.m_quitRequested = true;
    }
    else if (const auto* closeRequest = std::get_if<platform::WindowCloseRequested>(&event);
             closeRequest != nullptr && closeRequest->window == mainWindow)
    {
        m_application.m_quitRequested = true;
    }
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
    });
    if (!window)
    {
        DEVEX_LOG_FATAL("Cannot create the main window: {}", window.error());
        return EXIT_FAILURE;
    }

    const math::Extent2D pixelSize = window->pixelSize();
    DEVEX_LOG_DEBUG("Main window {}x{} ({}x{} pixels), fixed update at {} Hz", config.width,
                    config.height, pixelSize.width, pixelSize.height, config.fixedUpdateRate);

    detail::ApplicationRunner runner(application, *platform, *window);
    return runner.execute(config);
}

} // namespace devex::runtime
