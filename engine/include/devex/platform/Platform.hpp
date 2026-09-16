#pragma once

#include <devex/core/Error.hpp>
#include <devex/platform/Event.hpp>
#include <devex/platform/Input.hpp>
#include <devex/platform/Key.hpp>
#include <devex/platform/Window.hpp>

#include <chrono>
#include <functional>
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

    // Name of the key position, independent of the layout ("W").
    [[nodiscard]] std::string keyName(Key key) const;
    // Label of the key under the current keyboard layout ("Z" for Key::W on AZERTY).
    [[nodiscard]] std::string keyLabel(Key key) const;

private:
    Platform() = default;
    void shutdown() noexcept;

    bool m_initialized = false;
    Input m_input;
};

// Sleeps for the given duration with sub-millisecond precision.
void sleepPrecise(std::chrono::nanoseconds duration);

} // namespace devex::platform
