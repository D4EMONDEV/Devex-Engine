#pragma once

#include <devex/math/Math.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace devex::platform {

enum class WindowId : std::uint32_t
{
};

struct WindowConfig
{
    std::string title = "Devex";
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    bool resizable = true;
};

namespace detail {
struct NativeWindow;
} // namespace detail

// An operating system window, created by Platform::createWindow. It must be destroyed before the
// Platform that created it.
class Window
{
public:
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] WindowId id() const noexcept;
    // Size in window coordinates, which differ from pixels on high-density displays.
    [[nodiscard]] math::Extent2D size() const noexcept;
    // Size of the drawable area in pixels, as used by a swapchain.
    [[nodiscard]] math::Extent2D pixelSize() const noexcept;
    [[nodiscard]] bool isMinimized() const noexcept;

    void setTitle(std::string_view title);

    // A captured mouse is hidden and reports unbounded motion, as needed by first-person cameras.
    void setMouseCaptured(bool captured);
    [[nodiscard]] bool isMouseCaptured() const noexcept;

private:
    friend class Platform;

    explicit Window(detail::NativeWindow* native) noexcept;
    void destroy() noexcept;

    detail::NativeWindow* m_native = nullptr;
};

} // namespace devex::platform
