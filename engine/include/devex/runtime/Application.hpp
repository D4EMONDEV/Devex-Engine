#pragma once

#include <devex/core/Error.hpp>
#include <devex/core/Time.hpp>
#include <devex/platform/Event.hpp>
#include <devex/platform/Input.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/platform/Window.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/render/RenderWorld.hpp>
#include <devex/runtime/AssetRegistry.hpp>
#include <devex/scene/Scene.hpp>

#include <concepts>
#include <cstdint>
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
};

namespace detail {
class ApplicationRunner;
} // namespace detail

// Base class of every program driven by the engine loop. Each frame polls events, runs the fixed
// updates that are due, runs one variable update, updates the scene transforms, then renders the
// scene. Engine services are available from onStartup to onShutdown, not in the constructor.
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

protected:
    Application() = default;

    [[nodiscard]] const platform::Platform& platform() const noexcept;
    [[nodiscard]] const platform::Input& input() const noexcept;
    [[nodiscard]] platform::Window& window() noexcept;
    // The scene rendered every frame. It can be replaced, for instance by a loaded one.
    [[nodiscard]] scene::Scene& scene() noexcept;
    // Maps asset identifiers to loaded resources, with the built-in meshes registered.
    [[nodiscard]] AssetRegistry& assets() noexcept;
    // Only available when ApplicationConfig::enableRendering is set.
    [[nodiscard]] render::Renderer& renderer() noexcept;
    [[nodiscard]] const render::Renderer& renderer() const noexcept;

    // Progress towards the next fixed update in [0, 1), to interpolate between simulation states.
    [[nodiscard]] double interpolationAlpha() const noexcept;

    // Ends the loop after the current frame.
    void requestQuit() noexcept;

private:
    friend class detail::ApplicationRunner;

    platform::Platform* m_platform = nullptr;
    platform::Window* m_window = nullptr;
    render::Renderer* m_renderer = nullptr;
    scene::Scene* m_scene = nullptr;
    AssetRegistry* m_assets = nullptr;
    double m_interpolationAlpha = 0.0;
    bool m_quitRequested = false;
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
