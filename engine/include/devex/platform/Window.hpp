#pragma once

#include <devex/core/Export.hpp>

#include <devex/core/Error.hpp>
#include <devex/math/Math.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace devex::platform {

enum class WindowId : std::uint32_t
{
};

struct DEVEX_API WindowConfig
{
    std::string title = "Devex";
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    bool resizable = true;
    // Required to create a Vulkan surface for the window.
    bool vulkan = false;
    bool hidden = false;
    // Borderless over the whole display, at the resolution of the desktop.
    bool fullscreen = false;
};

namespace detail {
struct NativeWindow;
} // namespace detail

// A part of a display, in the coordinates of windows.
struct DEVEX_API ScreenArea
{
    int x = 0;
    int y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// An operating system window, created by Platform::createWindow. It must be destroyed before the
// Platform that created it.
class DEVEX_API Window
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
    [[nodiscard]] bool isMaximized() const noexcept;
    [[nodiscard]] bool isHidden() const noexcept;
    // Content scale of the display the window is on: 1.5 when the system scales it to 150 %.
    [[nodiscard]] float displayScale() const noexcept;

    void setTitle(std::string_view title);
    // Resizes the window, in window coordinates, and puts it back to its normal state first.
    void setSize(math::Extent2D size);
    // Centers the window on its display.
    void center();
    // What the display it is on leaves to windows, without the task bars and the like.
    [[nodiscard]] ScreenArea usableArea() const;
    // Fills a part of a display, its frame included.
    void fitInto(const ScreenArea& area);
    void maximize();
    // Leaves the maximized or minimized state.
    void restore();
    // Borderless over the whole display, at the resolution of the desktop.
    void setFullscreen(bool fullscreen);
    [[nodiscard]] bool isFullscreen() const noexcept;
    // Matches the title bar drawn by the system to the application's colors, where the system
    // allows it (Windows): dark or light, and a caption color given in sRGB from 0 to 1.
    void setTitleBarColors(bool dark, math::Vec3 caption);
    // The icon of the window and its taskbar button, from 8-bit RGBA pixels.
    void setIcon(std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height);

    // A captured mouse is hidden and reports unbounded motion, as needed by first-person cameras.
    void setMouseCaptured(bool captured);
    [[nodiscard]] bool isMouseCaptured() const noexcept;

    // Creates a VkSurfaceKHR for this window on the given VkInstance and returns its handle. The
    // window must have been created with WindowConfig::vulkan, and the caller owns the surface.
    [[nodiscard]] core::Result<std::uint64_t> createVulkanSurface(void* vulkanInstance) const;

private:
    friend class Platform;

    explicit Window(detail::NativeWindow* native) noexcept;
    void destroy() noexcept;

    detail::NativeWindow* m_native = nullptr;
};

} // namespace devex::platform
