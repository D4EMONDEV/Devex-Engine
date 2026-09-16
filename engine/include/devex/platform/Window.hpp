#pragma once

#include <devex/core/Error.hpp>
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
    // Required to create a Vulkan surface for the window.
    bool vulkan = false;
    bool hidden = false;
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
