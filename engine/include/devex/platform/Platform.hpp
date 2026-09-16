#pragma once

#include <devex/core/Error.hpp>
#include <devex/platform/Event.hpp>
#include <devex/platform/Input.hpp>
#include <devex/platform/Key.hpp>
#include <devex/platform/Window.hpp>

#include <chrono>
#include <filesystem>
#include <functional>
#include <span>
#include <string>

namespace devex::platform {

using EventCallback = std::function<void(const Event& event)>;

// Owns the operating system layer: windows, events and input. Only one instance may exist at a
// time, and it must outlive every window it creates.
class Platform
{
public:
    [[nodiscard]] static core::Result<Platform> create();

    Platform(Platform&& other) noexcept;
    Platform& operator=(Platform&& other) noexcept;
    ~Platform();

    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;

    [[nodiscard]] core::Result<Window> createWindow(const WindowConfig& config);

    // Starts a new input frame, then updates the input state and forwards every pending event.
    void pollEvents(const EventCallback& callback);

    [[nodiscard]] const Input& input() const noexcept;

    // Directory containing the executable, where engine data such as shaders is deployed.
    [[nodiscard]] std::filesystem::path baseDirectory() const;

    // Name of the key position, independent of the layout ("W").
    [[nodiscard]] std::string keyName(Key key) const;
    // Label of the key under the current keyboard layout ("Z" for Key::W on AZERTY).
    [[nodiscard]] std::string keyLabel(Key key) const;

    // Instance extensions needed to create window surfaces. Available once a window has been
    // created with WindowConfig::vulkan; the strings live as long as the Platform.
    [[nodiscard]] core::Result<std::span<const char* const>> vulkanInstanceExtensions() const;

    // Registers the function called when a window must be redrawn while the operating system
    // blocks pollEvents, such as during a live resize on Windows. An empty function unregisters.
    void setLiveRedrawCallback(std::function<void()> callback);

    // Connects Dear ImGui to the window: pollEvents then forwards every event to ImGui. An ImGui
    // context must be current, and shutdownImGui must run before it is destroyed.
    [[nodiscard]] core::Result<void> initializeImGui(Window& window);
    void shutdownImGui() noexcept;
    // Updates ImGui's display size, time and mouse state for a new ImGui frame.
    void beginImGuiFrame();
    // While ImGui uses a device, presses and motion on it no longer reach Input. Releases always
    // do, so that no key stays down.
    void setImGuiInputCapture(bool keyboard, bool mouse) noexcept;

private:
    Platform() = default;
    void shutdown() noexcept;

    bool m_initialized = false;
    bool m_imguiInitialized = false;
    bool m_imguiCapturesKeyboard = false;
    bool m_imguiCapturesMouse = false;
    Input m_input;
};

// Sleeps for the given duration with sub-millisecond precision.
void sleepPrecise(std::chrono::nanoseconds duration);

} // namespace devex::platform
