#include "SdlWindow.hpp"

#include <devex/core/Assert.hpp>
#include <devex/core/Log.hpp>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_vulkan.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <dwmapi.h>
#endif

#include <algorithm>
#include <bit>
#include <string>
#include <utility>

namespace devex::platform {

Window::Window(detail::NativeWindow* native) noexcept
    : m_native(native)
{
}

Window::Window(Window&& other) noexcept
    : m_native(std::exchange(other.m_native, nullptr))
{
}

Window& Window::operator=(Window&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        m_native = std::exchange(other.m_native, nullptr);
    }
    return *this;
}

Window::~Window()
{
    destroy();
}

void Window::destroy() noexcept
{
    if (m_native != nullptr)
    {
        SDL_DestroyWindow(detail::toSdlWindow(m_native));
        m_native = nullptr;
    }
}

WindowId Window::id() const noexcept
{
    DEVEX_ASSERT(m_native != nullptr);
    return WindowId{SDL_GetWindowID(detail::toSdlWindow(m_native))};
}

math::Extent2D Window::size() const noexcept
{
    DEVEX_ASSERT(m_native != nullptr);
    return detail::windowSize(detail::toSdlWindow(m_native));
}

math::Extent2D Window::pixelSize() const noexcept
{
    DEVEX_ASSERT(m_native != nullptr);
    return detail::windowPixelSize(detail::toSdlWindow(m_native));
}

bool Window::isMinimized() const noexcept
{
    DEVEX_ASSERT(m_native != nullptr);
    return (SDL_GetWindowFlags(detail::toSdlWindow(m_native)) & SDL_WINDOW_MINIMIZED) != 0;
}

bool Window::isMaximized() const noexcept
{
    DEVEX_ASSERT(m_native != nullptr);
    return (SDL_GetWindowFlags(detail::toSdlWindow(m_native)) & SDL_WINDOW_MAXIMIZED) != 0;
}

bool Window::isHidden() const noexcept
{
    DEVEX_ASSERT(m_native != nullptr);
    return (SDL_GetWindowFlags(detail::toSdlWindow(m_native)) & SDL_WINDOW_HIDDEN) != 0;
}

float Window::displayScale() const noexcept
{
    DEVEX_ASSERT(m_native != nullptr);
    const float scale = SDL_GetWindowDisplayScale(detail::toSdlWindow(m_native));
    return scale > 0.0f ? scale : 1.0f;
}

void Window::setSize(math::Extent2D size)
{
    DEVEX_ASSERT(m_native != nullptr);
    SDL_Window* const window = detail::toSdlWindow(m_native);
    SDL_RestoreWindow(window);
    SDL_SetWindowSize(window, static_cast<int>(size.width), static_cast<int>(size.height));
}

void Window::center()
{
    DEVEX_ASSERT(m_native != nullptr);
    SDL_SetWindowPosition(detail::toSdlWindow(m_native), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}

void Window::maximize()
{
    DEVEX_ASSERT(m_native != nullptr);
    SDL_MaximizeWindow(detail::toSdlWindow(m_native));
}

void Window::restore()
{
    DEVEX_ASSERT(m_native != nullptr);
    SDL_RestoreWindow(detail::toSdlWindow(m_native));
}

void Window::setTitleBarColors([[maybe_unused]] bool dark, [[maybe_unused]] math::Vec3 caption)
{
    DEVEX_ASSERT(m_native != nullptr);
#ifdef _WIN32
    const auto hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(detail::toSdlWindow(m_native)),
                                                               SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (hwnd == nullptr)
    {
        return;
    }
    // Attributes unknown to older versions of Windows are ignored there.
    const BOOL useDark = dark ? TRUE : FALSE;
    static_cast<void>(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark)));
    const auto channel = [](float value) {
        return static_cast<BYTE>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    const COLORREF color = RGB(channel(caption.r), channel(caption.g), channel(caption.b));
    static_cast<void>(DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &color, sizeof(color)));
#endif
}

void Window::setIcon(std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height)
{
    DEVEX_ASSERT(m_native != nullptr);
    DEVEX_ASSERT(rgba.size() == std::size_t{width} * height * 4);
    SDL_Surface* const surface = SDL_CreateSurfaceFrom(static_cast<int>(width), static_cast<int>(height),
                                                       SDL_PIXELFORMAT_ABGR8888, const_cast<std::uint8_t*>(rgba.data()),
                                                       static_cast<int>(width * 4));
    if (surface == nullptr)
    {
        DEVEX_LOG_WARNING("Cannot set the window icon: {}", SDL_GetError());
        return;
    }
    if (!SDL_SetWindowIcon(detail::toSdlWindow(m_native), surface))
    {
        DEVEX_LOG_WARNING("Cannot set the window icon: {}", SDL_GetError());
    }
    SDL_DestroySurface(surface);
}

void Window::setTitle(std::string_view title)
{
    DEVEX_ASSERT(m_native != nullptr);
    const std::string terminatedTitle(title);
    SDL_SetWindowTitle(detail::toSdlWindow(m_native), terminatedTitle.c_str());
}

void Window::setMouseCaptured(bool captured)
{
    DEVEX_ASSERT(m_native != nullptr);
    if (!SDL_SetWindowRelativeMouseMode(detail::toSdlWindow(m_native), captured))
    {
        DEVEX_LOG_WARNING("Cannot {} the mouse: {}", captured ? "capture" : "release",
                          SDL_GetError());
    }
}

bool Window::isMouseCaptured() const noexcept
{
    DEVEX_ASSERT(m_native != nullptr);
    return SDL_GetWindowRelativeMouseMode(detail::toSdlWindow(m_native));
}

core::Result<std::uint64_t> Window::createVulkanSurface(void* vulkanInstance) const
{
    DEVEX_ASSERT(m_native != nullptr);
    DEVEX_ASSERT(vulkanInstance != nullptr);

    VkSurfaceKHR surface{};
    if (!SDL_Vulkan_CreateSurface(detail::toSdlWindow(m_native),
                                  static_cast<VkInstance>(vulkanInstance), nullptr, &surface))
    {
        return core::makeError(core::ErrorCode::Platform, "cannot create a Vulkan surface: {}",
                               SDL_GetError());
    }
    return std::bit_cast<std::uint64_t>(surface);
}

} // namespace devex::platform
