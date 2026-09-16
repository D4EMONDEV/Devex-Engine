#include "SdlWindow.hpp"

#include <devex/core/Assert.hpp>
#include <devex/core/Log.hpp>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_vulkan.h>

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
